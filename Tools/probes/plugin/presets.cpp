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
//   presets.apply.globals_touched      mode aside, the 8 globals (bypass, quality, ..., output) keep their values
//   presets.concurrent.*               every factory preset applied 3 times through the processor's own PresetAccess
//                                      while a second thread runs processBlock: no non-finite output, no batch left
//                                      open, the last preset's values in place
//   presets.modified.*                 a one-step nudge of a preset parameter reads modified and restoring it does not;
//                                      a Mode switch reads modified, switching back does not; bypass/quality/listen
//                                      never do, nor does output; revision() bumps on each flip
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
// PresetAccess management (P3b, S12.4; S12 lead revision 8), on a fresh instance with two user presets of its own, A and
// B (current). Every call goes through one checker: a success must bump revision(), a refusal must return false with
// the list, current(), modified() and revision() unchanged.
//   presets.rename.user                A (not current) renamed, trimmed, in the list and the store, the selection kept;
//                                      B (current) renamed: current() follows, unmodified, the session's <PRESET> names
//                                      it; B to its own name in another case; no parameter moves (all 29 bitwise)
//   presets.rename.refused             factory rows; a user or factory name in any case; empty or blank; rows -1,
//                                      count(), INT_MAX
//   presets.export.rows                a factory row: the file holds the bank's uuid, name, category, notes, values
//                                      bitwise and attributes; a user row (tagged and used in the store): the store's
//                                      copy, without tags or timestamps; both files hold nothing but identity,
//                                      metadata, ATTR and PARAM
//   presets.export.refused             rows -1, count(); an empty or relative path; a directory (empty or not); a parent
//                                      that is a file: no file appears, no directory is replaced
//   presets.remove.user                A (not current): gone from the list and the store, the selection kept; B
//                                      (current, modified): current() -1, no parameter moves, still modified (the same
//                                      baseline), the session's <PRESET> names no preset
//   presets.remove.refused             factory rows (first, FET 76, last); rows -1, count(), INT_MIN
//   presets.import.roundtrip           A's exported file after A's removal: A is back (its uuid, name, fet-76), the
//                                      store's copy bitwise equal to A's before the export incl. modeId/modeRev, no
//                                      tags; applied: its Mode and values
//   presets.import.duplicate           the same file again and the FET 76 factory file: successes that write nothing
//   presets.import.fresh               A's file with one value changed: a fresh uuid, "Probe Manage A1 2", the values
//   presets.import.no_modeid           a file without modeId is imported and lists as clean
//   presets.import.refused             an unknown modeId; missing, not XML, empty, a directory; another product's root or
//                                      plugin; a newer format; no name; a value that is not a number; empty or
//                                      relative paths
// Save over a user preset (P3c, S12.5; S12 lead revision 11), through the same checker:
//   presets.overwrite.user             A current, switched to bus-g with new values (modified), then overwrite(A):
//                                      revision() bumps exactly once; A is current and unmodified, listed with its
//                                      name and category and Mode bus-g; the store's A keeps uuid, name, category,
//                                      author, notes, tags, created and last-used times, and holds the 22 live values
//                                      bitwise with modeId bus-g and its modeRev; no parameter moved; the session
//                                      names A
//   presets.overwrite.reapply_bitwise  after another preset, applying A again restores all 29 values bitwise (the Mode
//                                      among them), unmodified
//   presets.overwrite.not_current      a user row that is not current is saved over and becomes current; A untouched
//   presets.overwrite.refused          factory rows (first, FET 76, last) and rows -1, count(), INT_MAX, INT_MIN:
//                                      false, nothing changes (the store's factory rows included), the live sound
//                                      stays modified
//   presets.overwrite.store_failure    a row another connection deleted after the list was read: false; the list, the
//                                      selection and modified() stay and nothing is re-created; the next poll drops it
//   presets.manage.success_no_bump / .refusal_changed   the checker's two counts (0)
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
#include <limits>
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

    // The same parameters (ids, float bits) and attributes (keys, values), each id and key once, order aside.
    bool sameContent(const Preset& a, const Preset& b)
    {
        if (a.params.size() != b.params.size() || a.attributes.size() != b.attributes.size())
            return false;
        for (const fp::ParamValue& v : a.params)
            if (const fp::ParamValue* w = b.find(v.id); w == nullptr || bitsOf(w->value) != bitsOf(v.value))
                return false;
        for (const fp::Attribute& x : a.attributes)
            if (const fp::Attribute* y = b.attr(x.key); y == nullptr || y->value != x.value)
                return false;
        return true;
    }

    // An exported file carries identity, metadata, ATTR and PARAM only: never tags or timestamps (PresetFile).
    bool fileIsImpersonal(const juce::File& f)
    {
        const std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(f);
        if (xml == nullptr)
            return false;
        constexpr std::array<const char*, 7> kAllowed{ "format", "plugin", "uuid", "name", "category", "author", "notes" };
        for (int i = 0; i < xml->getNumAttributes(); ++i)
        {
            const juce::String name = xml->getAttributeName(i);
            if (std::none_of(kAllowed.begin(), kAllowed.end(), [&name](const char* k) { return name == k; }))
                return false;
        }
        for (const juce::XmlElement* e : xml->getChildIterator())
            if (!e->hasTagName("ATTR") && !e->hasTagName("PARAM"))
                return false;
        return true;
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
        fcmp::EditAccess& edits() override { return proc_.edits(); }

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
    // The real database, which this probe must never open: FunkPresets' default location (ADR-92: ~/.config on Linux).
   #if JUCE_LINUX || JUCE_BSD
    const juce::File realDb = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("FCompressor/Presets.db");
   #else
    const juce::File realDb = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("Application Support/FCompressor/Presets.db");
   #endif
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
        const std::array<std::pair<Pid, float>, 7> globals{ { { Pid::extkey, 0.7f }, { Pid::listen, 1.0f },
                                                              { Pid::delta, 1.0f }, { Pid::bypass, 0.6f },
                                                              { Pid::quality, 2.0f }, { Pid::labudget, 1.0f },
                                                              { Pid::output, -5.5f } } };
        for (const auto& [pid, v] : globals)
            setPlain(*b, pid, v);
        std::array<float, 7> before{};
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
        setPlain(*a, Pid::output, -9.0f);                         // ADR-88: never MODIFIED
        bad += pa.modified() ? 1 : 0;
        setPlain(*a, Pid::bypass, 0.0f);
        setPlain(*a, Pid::quality, 1.0f);
        setPlain(*a, Pid::listen, 0.0f);
        setPlain(*a, Pid::output, 0.0f);
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

    // ---- user-preset management: rename, remove, import, export (P3b, S12 lead revision 8) ------------------------------
    {
        auto m = running();
        fcmp::PresetAccess& pm = m->presets();
        const juce::String ext = cfg.fileExtension;
        const juce::File dir = workDir.getChildFile("p3b");      // inside the sandbox (or the hand run's scratch dir)
        const auto pathOf = [](const juce::File& f) { return f.getFullPathName().toStdString(); };
        const auto listing = [&pm] {
            std::vector<std::string> rows;
            for (int i = 0; i < pm.count(); ++i)
            {
                const fcmp::PresetAccess::Row r = pm.row(i);
                rows.push_back(r.uuid + '\n' + r.name + '\n' + r.category + '\n' + r.modeKey);
            }
            return rows;
        };
        const auto rawBits = [&m] {
            std::array<std::uint32_t, fcdsp::kNumParams> b{};
            for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
                b[i] = bitsOf(m->rawValue(static_cast<Pid>(i)));
            return b;
        };
        // One management call: a success bumps revision(); a refusal returns false with the list, the selection,
        // modified() and revision() exactly as they were.
        std::int64_t successStill = 0, refusalMoved = 0, successes = 0, refusals = 0;
        const auto call = [&](auto&& fn) -> bool {
            const std::uint32_t rev = pm.revision();
            const std::vector<std::string> rows = listing();
            const int cur = pm.current();
            const bool mod = pm.modified();
            const bool ok = fn();
            const std::uint32_t after = pm.revision();
            if (ok)
            {
                ++successes;
                successStill += after != rev ? 0 : 1;
            }
            else
            {
                ++refusals;
                refusalMoved += after == rev && listing() == rows && pm.current() == cur && pm.modified() == mod ? 0 : 1;
            }
            return ok;
        };
        const fcdsp::ModeEntry* fet = fcdsp::byKey("fet-76");
        const int fetSlot = fet != nullptr ? fcdsp::slotOf(*fet) : -1;

        // two user presets of our own: A (FET 76 at odd values) and B, which stays current
        pm.apply(fetRow);
        setPlain(*m, Pid::thr, -27.25f);
        setPlain(*m, Pid::rel, 250.0f);
        bool setup = pm.saveAs("Probe Manage A", "Bus");
        const juce::String uuidA = pm.current() >= 0 ? juce::String(pm.row(pm.current()).uuid) : juce::String();
        setPlain(*m, Pid::thr, -18.0f);
        setup = setup && pm.saveAs("Probe Manage B", "Bus");
        const juce::String uuidB = pm.current() >= 0 ? juce::String(pm.row(pm.current()).uuid) : juce::String();
        if (!setup || uuidA.isEmpty() || uuidB.isEmpty() || uuidA == uuidB || fetSlot < 0)
        {
            P.harnessError("proc.presets: could not save the two user presets the management rows start from");
            return P.finish();
        }

        // rename: A (not current), then B (current: the session's identity follows), then B in another case
        {
            std::int64_t bad = 0;
            const auto before = rawBits();
            bad += call([&] { return pm.rename(indexOfUuid(pm, uuidA), "  Probe Manage A1 "); }) ? 0 : 1;
            const int rowA = indexOfUuid(pm, uuidA);
            const std::optional<Preset> sa = store->get(uuidA);
            bad += rowA >= 0 && pm.row(rowA).name == "Probe Manage A1" && sa.has_value() && sa->name == "Probe Manage A1"
                       ? 0 : 1;
            bad += pm.current() == indexOfUuid(pm, uuidB) ? 0 : 1;              // the selection stays on B

            bad += call([&] { return pm.rename(indexOfUuid(pm, uuidB), "Probe Manage B1"); }) ? 0 : 1;
            const int cur = pm.current();
            bad += cur >= 0 && cur == indexOfUuid(pm, uuidB) && pm.row(cur).name == "Probe Manage B1" && !pm.modified()
                       ? 0 : 1;
            const std::unique_ptr<juce::XmlElement> xml = xmlOf(save(*m));
            const juce::XmlElement* preset = xml != nullptr ? xml->getChildByName("PRESET") : nullptr;
            bad += preset != nullptr && preset->getStringAttribute("uuid") == uuidB
                           && preset->getStringAttribute("name") == "Probe Manage B1"
                       ? 0 : 1;

            bad += call([&] { return pm.rename(indexOfUuid(pm, uuidB), "probe manage b1"); }) ? 0 : 1;   // its own
            bad += pm.row(indexOfUuid(pm, uuidB)).name == "probe manage b1" ? 0 : 1;
            bad += rawBits() == before ? 0 : 1;                                 // no parameter moved
            P.eq("presets.rename.user", bad, 0);
        }
        {
            std::int64_t bad = 0;
            const int rowA = indexOfUuid(pm, uuidA);
            const auto refused = [&](int index, const juce::String& name) {
                bad += call([&] { return pm.rename(index, name.toStdString()); }) ? 1 : 0;
            };
            refused(0, "Probe Init");                                           // factory rows
            refused(fetRow, "Probe FET");
            refused(rowA, "Probe Manage B1");                                   // a user preset's name, any case
            refused(rowA, "PROBE MANAGE B1");
            refused(rowA, bank[1].name);                                        // a factory preset's name
            refused(rowA, "init");
            refused(rowA, "");                                                  // no name
            refused(rowA, "   ");
            refused(-1, "Probe X");                                             // no row
            refused(pm.count(), "Probe X");
            refused(std::numeric_limits<int>::max(), "Probe X");
            bad += pm.row(indexOfUuid(pm, uuidA)).name == "Probe Manage A1" ? 0 : 1;
            bad += store->get(bank[1].uuid).has_value() && store->get(bank[1].uuid)->name == bank[1].name ? 0 : 1;
            P.eq("presets.rename.refused", bad, 0);
        }

        // export: a factory row (the compiled bank's preset) and a user row (the store's copy, tagged and used, so a
        // file that carried either would show it)
        store->setTags(uuidA, fp::tagBit(fp::Tag::red));
        store->markUsed(uuidA);
        pm.revision();
        const juce::File factoryFile = dir.getChildFile("factory" + ext);
        const juce::File userFile = dir.getChildFile("user" + ext);
        {
            std::int64_t bad = 0;
            bad += call([&] { return pm.exportFile(fetRow, pathOf(factoryFile)); }) ? 0 : 1;
            const Preset& f = bank[static_cast<std::size_t>(fetRow)];
            const std::optional<Preset> fb = fp::PresetFile::read(cfg, factoryFile);
            bad += fb.has_value() && fb->uuid == f.uuid && fb->name == f.name && fb->category == f.category
                           && fb->notes == f.notes && sameContent(*fb, f)
                       ? 0 : 1;
            bad += fileIsImpersonal(factoryFile) ? 0 : 1;

            bad += call([&] { return pm.exportFile(indexOfUuid(pm, uuidA), pathOf(userFile)); }) ? 0 : 1;
            const std::optional<Preset> ub = fp::PresetFile::read(cfg, userFile);
            const std::optional<Preset> us = store->get(uuidA);
            bad += ub.has_value() && us.has_value() && us->tags != 0 && us->lastUsedMs != 0 && ub->uuid == us->uuid
                           && ub->name == us->name && ub->category == us->category && sameContent(*ub, *us)
                           && ub->tags == 0 && ub->createdMs == 0 && ub->lastUsedMs == 0
                       ? 0 : 1;
            bad += fileIsImpersonal(userFile) ? 0 : 1;
            P.eq("presets.export.rows", bad, 0);
        }
        {
            std::int64_t bad = 0;
            const juce::File never = dir.getChildFile("refused" + ext);
            const juce::File plain = dir.getChildFile("plain.txt");
            plain.replaceWithText("not a directory");
            const auto refused = [&](int index, const std::string& path) {
                bad += call([&] { return pm.exportFile(index, path); }) ? 1 : 0;
            };
            refused(-1, pathOf(never));                                         // no row
            refused(pm.count(), pathOf(never));
            refused(0, "");                                                     // no path, a relative one
            refused(0, ("probe-relative" + ext).toStdString());
            const juce::File emptyDir = dir.getChildFile("empty-dir" + ext);
            emptyDir.createDirectory();
            refused(0, pathOf(dir));                                            // a directory, an empty one
            refused(0, pathOf(emptyDir));
            refused(0, pathOf(plain.getChildFile("under-a-file" + ext)));       // a parent that is a file
            bad += never.exists() ? 1 : 0;
            bad += juce::File::getCurrentWorkingDirectory().getChildFile("probe-relative" + ext).exists() ? 1 : 0;
            bad += dir.isDirectory() && emptyDir.isDirectory() && plain.existsAsFile() ? 0 : 1;
            P.eq("presets.export.refused", bad, 0);
        }

        // remove: A (not current), then B (current, and modified: it stays modified against the same baseline)
        const std::optional<Preset> storedA = store->get(uuidA);
        {
            std::int64_t bad = 0;
            const int n = pm.count();
            bad += call([&] { return pm.remove(indexOfUuid(pm, uuidA)); }) ? 0 : 1;
            bad += pm.count() == n - 1 && indexOfUuid(pm, uuidA) < 0 && !store->get(uuidA).has_value() ? 0 : 1;
            bad += pm.current() >= 0 && pm.current() == indexOfUuid(pm, uuidB) ? 0 : 1;

            setPlain(*m, Pid::thr, -17.0f);                                     // B, modified
            const auto before = rawBits();
            bad += pm.modified() ? 0 : 1;
            bad += call([&] { return pm.remove(pm.current()); }) ? 0 : 1;
            bad += pm.current() == -1 && pm.count() == n - 2 && indexOfUuid(pm, uuidB) < 0 ? 0 : 1;
            bad += rawBits() == before ? 0 : 1;                                 // no parameter moved
            bad += pm.modified() ? 0 : 1;                                       // the same baseline
            const std::unique_ptr<juce::XmlElement> xml = xmlOf(save(*m));
            const juce::XmlElement* preset = xml != nullptr ? xml->getChildByName("PRESET") : nullptr;
            bad += preset != nullptr && preset->getStringAttribute("uuid").isEmpty()
                           && preset->getStringAttribute("name").isEmpty()
                       ? 0 : 1;                                                 // the session names no deleted preset
            P.eq("presets.remove.user", bad, 0);
        }
        {
            std::int64_t bad = 0;
            const auto refused = [&](int index) { bad += call([&] { return pm.remove(index); }) ? 1 : 0; };
            refused(0);                                                         // factory rows
            refused(fetRow);
            refused(static_cast<int>(nBank) - 1);
            refused(-1);                                                        // no row
            refused(pm.count());
            refused(std::numeric_limits<int>::min());
            bad += store->count(fp::Source::factory) == static_cast<int>(nBank) ? 0 : 1;
            P.eq("presets.remove.refused", bad, 0);
        }

        // import: A's file brings A back (the store keeps a uuid it lacks) with every value bit for bit and
        // modeId/modeRev as saved; the same file again, and the factory file, are duplicates (a success, nothing
        // written); a changed copy gets a fresh uuid and a unique name; a file without modeId is imported and loads clean
        {
            std::int64_t bad = 0;
            const int n = pm.count();
            bad += call([&] { return pm.importFile(pathOf(userFile)); }) ? 0 : 1;
            const int rowA = indexOfUuid(pm, uuidA);
            const std::optional<Preset> back = store->get(uuidA);
            bad += pm.count() == n + 1 && rowA >= 0 && !pm.row(rowA).factory && pm.row(rowA).name == "Probe Manage A1"
                           && pm.row(rowA).modeKey == "fet-76"
                       ? 0 : 1;
            bad += storedA.has_value() && back.has_value() && sameContent(*back, *storedA) && back->tags == 0
                           && back->attr(fcmp::factory::kModeRevAttr) != nullptr
                       ? 0 : 1;
            if (rowA >= 0 && back.has_value())
            {
                pm.apply(rowA);
                bad += m->currentRaw().modeSlot == fetSlot && valueError(*m, *back) <= 1e-6 && pm.current() == rowA
                               && !pm.modified()
                           ? 0 : 1;
            }
            P.eq("presets.import.roundtrip", bad, 0);

            bad = 0;
            bad += call([&] { return pm.importFile(pathOf(userFile)); }) ? 0 : 1;
            bad += call([&] { return pm.importFile(pathOf(factoryFile)); }) ? 0 : 1;
            bad += pm.count() == n + 1 && store->count(fp::Source::factory) == static_cast<int>(nBank) ? 0 : 1;
            P.eq("presets.import.duplicate", bad, 0);

            bad = 0;
            std::optional<Preset> changed = fp::PresetFile::read(cfg, userFile);
            const juce::File changedFile = dir.getChildFile("changed" + ext);
            if (changed.has_value() && changed->find("thr") != nullptr)
            {
                for (fp::ParamValue& v : changed->params)
                    if (v.id == "thr")
                        v.value = -11.5f;
                bad += fp::PresetFile::write(cfg, *changed, changedFile) ? 0 : 1;
                bad += call([&] { return pm.importFile(pathOf(changedFile)); }) ? 0 : 1;
                bad += pm.count() == n + 2 && indexOfUuid(pm, uuidA) >= 0 ? 0 : 1;   // A itself untouched
                int c = -1;
                for (int i = static_cast<int>(nBank); i < pm.count() && c < 0; ++i)
                    c = pm.row(i).name == "Probe Manage A1 2" ? i : -1;
                const std::optional<Preset> got = c >= 0 ? store->get(juce::String(pm.row(c).uuid)) : std::nullopt;
                bad += got.has_value() && got->uuid != uuidA && !got->isFactory && sameContent(*got, *changed) ? 0 : 1;
            }
            else
                ++bad;
            P.eq("presets.import.fresh", bad, 0);

            bad = 0;
            const juce::File noMode = dir.getChildFile("no-mode" + ext);
            noMode.replaceWithText("<FCompressorPreset format=\"1\" plugin=\"FCompressor\" name=\"Probe File No Mode\">"
                                   "<PARAM id=\"thr\" value=\"-20\"/></FCompressorPreset>");
            bad += call([&] { return pm.importFile(pathOf(noMode)); }) ? 0 : 1;
            int nm = -1;
            for (int i = static_cast<int>(nBank); i < pm.count() && nm < 0; ++i)
                nm = pm.row(i).name == "Probe File No Mode" ? i : -1;
            bad += nm >= 0 && pm.row(nm).modeKey == "clean" ? 0 : 1;
            P.eq("presets.import.no_modeid", bad, 0);
        }
        {
            std::int64_t bad = 0;
            const auto refused = [&](const std::string& path) {
                bad += call([&] { return pm.importFile(path); }) ? 1 : 0;
            };
            const auto file = [&dir, &ext](const char* stem, const juce::String& text) {
                const juce::File f = dir.getChildFile(stem + ext);
                if (text.isEmpty())
                    f.create();                                                 // an empty file (replaceWithText
                else                                                            // needs text to write)
                    f.replaceWithText(text);
                return f;
            };
            std::optional<Preset> unknown = fp::PresetFile::read(cfg, userFile);
            if (unknown.has_value())
            {
                unknown->uuid = juce::Uuid().toDashedString();
                unknown->name = "Probe File Unknown Mode";
                unknown->setAttr(fcmp::factory::kModeIdAttr, "no-such-mode");
                const juce::File f = dir.getChildFile("unknown-mode" + ext);
                bad += fp::PresetFile::write(cfg, *unknown, f) ? 0 : 1;
                refused(pathOf(f));                                             // a Mode this build lacks
            }
            else
                ++bad;
            refused(pathOf(dir.getChildFile("missing" + ext)));                 // unreadable
            refused(pathOf(file("garbage", "this is not a preset")));
            refused(pathOf(file("empty", "")));
            refused(pathOf(dir));                                               // directories
            refused(pathOf(dir.getChildFile("empty-dir" + ext)));
            refused(pathOf(file("foreign-root", "<HardwareReverbPreset format=\"1\" plugin=\"HardwareReverb\" "
                                                "name=\"Hall\"><PARAM id=\"thr\" value=\"-10\"/></HardwareReverbPreset>")));
            refused(pathOf(file("foreign-plugin", "<FCompressorPreset format=\"1\" plugin=\"HardwareReverb\" "
                                                  "name=\"Hall\"><PARAM id=\"thr\" value=\"-10\"/></FCompressorPreset>")));
            refused(pathOf(file("newer-format", "<FCompressorPreset format=\"2\" plugin=\"FCompressor\" "
                                                "name=\"Probe Newer\"><PARAM id=\"thr\" value=\"-10\"/></FCompressorPreset>")));
            refused(pathOf(file("no-name", "<FCompressorPreset format=\"1\" plugin=\"FCompressor\" name=\" \">"
                                           "<PARAM id=\"thr\" value=\"-10\"/></FCompressorPreset>")));
            refused(pathOf(file("bad-value", "<FCompressorPreset format=\"1\" plugin=\"FCompressor\" name=\"Probe Bad\">"
                                             "<PARAM id=\"thr\" value=\"loud\"/></FCompressorPreset>")));
            refused("");                                                        // no path, a relative one
            refused(("user" + ext).toStdString());
            P.eq("presets.import.refused", bad, 0);
        }

        // overwrite (P3c, S12.5; S12 lead revision 11): A is current (applied by the round trip above); a new Mode and
        // new values, then a save over A. Then a user row that is not current. Every call through the checker.
        const juce::String uuidC = [&pm, nBank] {
            for (int i = static_cast<int>(nBank); i < pm.count(); ++i)
                if (pm.row(i).name == "Probe Manage A1 2")
                    return juce::String(pm.row(i).uuid);
            return juce::String();
        }();
        const fcdsp::ModeEntry* busG = fcdsp::byKey("bus-g");
        const int busGSlot = busG != nullptr ? fcdsp::slotOf(*busG) : -1;
        if (uuidC.isEmpty() || busGSlot < 0 || pm.current() != indexOfUuid(pm, uuidA))
        {
            P.harnessError("proc.presets: the overwrite rows need A current, 'Probe Manage A1 2' and bus-g");
            return P.finish();
        }
        std::array<std::uint32_t, fcdsp::kNumParams> savedBits{};
        {
            std::int64_t bad = 0;
            store->setTags(uuidA, fp::tagBit(fp::Tag::green));                 // personal: kept by the overwrite
            pm.revision();
            const std::optional<Preset> before = store->get(uuidA);
            setPlain(*m, Pid::mode, static_cast<float>(busGSlot));             // the edit: another Mode, new values
            setPlain(*m, Pid::thr, -21.75f);
            setPlain(*m, Pid::rel, 180.0f);
            bad += pm.modified() ? 0 : 1;
            savedBits = rawBits();
            const std::uint32_t rev = pm.revision();
            bad += call([&] { return pm.overwrite(indexOfUuid(pm, uuidA)); }) ? 0 : 1;
            bad += pm.revision() == rev + 1 ? 0 : 1;                            // exactly one bump
            const int cur = pm.current();
            bad += cur >= 0 && cur == indexOfUuid(pm, uuidA) && !pm.modified() ? 0 : 1;
            const fcmp::PresetAccess::Row r = pm.row(cur);
            bad += r.name == "Probe Manage A1" && r.category == "Bus" && r.modeKey == "bus-g" && !r.factory ? 0 : 1;
            const std::optional<Preset> after = store->get(uuidA);
            if (before.has_value() && after.has_value())
            {
                bad += after->uuid == uuidA && after->name == before->name && after->category == before->category
                               && after->author == before->author && after->notes == before->notes && !after->isFactory
                               && after->tags == fp::tagBit(fp::Tag::green) && after->createdMs == before->createdMs
                               && after->lastUsedMs == before->lastUsedMs && after->modifiedMs >= before->modifiedMs
                           ? 0 : 1;
                const fp::Attribute* id = after->attr(fcmp::factory::kModeIdAttr);
                const fp::Attribute* rv = after->attr(fcmp::factory::kModeRevAttr);
                bad += id != nullptr && id->value == "bus-g" && rv != nullptr
                               && rv->value == juce::String(static_cast<int>(busG->desc->revision))
                           ? 0 : 1;
                bad += after->params.size() == fcdsp::kNumModeParams ? 0 : 1;
                for (const fp::ParamValue& v : after->params)
                    if (const Pid pid = pidOf(v.id);
                        pid == fcdsp::kNoPid || bitsOf(v.value) != bitsOf(m->rawValue(pid)))
                        ++bad;
            }
            else
                ++bad;
            bad += rawBits() == savedBits ? 0 : 1;                              // no parameter moved
            const std::unique_ptr<juce::XmlElement> xml = xmlOf(save(*m));
            const juce::XmlElement* preset = xml != nullptr ? xml->getChildByName("PRESET") : nullptr;
            bad += preset != nullptr && preset->getStringAttribute("uuid") == uuidA
                           && preset->getStringAttribute("name") == "Probe Manage A1"
                       ? 0 : 1;                                                 // the session names A
            P.eq("presets.overwrite.user", bad, 0);
        }
        {
            // Re-applied after another preset, A brings back exactly what was saved: all 29 values bit for bit (the
            // Mode among them), unmodified.
            std::int64_t bad = 0;
            pm.apply(fetRow);
            bad += rawBits() != savedBits ? 0 : 1;                              // the other preset moved something
            pm.apply(indexOfUuid(pm, uuidA));
            bad += rawBits() == savedBits && m->currentRaw().modeSlot == busGSlot ? 0 : 1;
            bad += pm.current() == indexOfUuid(pm, uuidA) && !pm.modified() ? 0 : 1;
            P.eq("presets.overwrite.reapply_bitwise", bad, 0);
        }
        {
            // A user row that is not current: saved over, and it becomes current; A is untouched.
            std::int64_t bad = 0;
            const std::optional<Preset> aBefore = store->get(uuidA);
            setPlain(*m, Pid::thr, -9.5f);
            bad += call([&] { return pm.overwrite(indexOfUuid(pm, uuidC)); }) ? 0 : 1;
            const int cur = pm.current();
            bad += cur >= 0 && cur == indexOfUuid(pm, uuidC) && !pm.modified()
                           && pm.row(cur).name == "Probe Manage A1 2"
                       ? 0 : 1;
            const std::optional<Preset> c = store->get(uuidC);
            const fp::ParamValue* thr = c.has_value() ? c->find("thr") : nullptr;
            bad += thr != nullptr && bitsOf(thr->value) == bitsOf(m->rawValue(Pid::thr)) ? 0 : 1;
            const std::optional<Preset> aAfter = store->get(uuidA);
            bad += aBefore.has_value() && aAfter.has_value() && sameContent(*aBefore, *aAfter) ? 0 : 1;
            P.eq("presets.overwrite.not_current", bad, 0);
        }
        {
            // Refused: factory rows and rows out of range; the bank's stored rows and the modified live sound stay.
            std::int64_t bad = 0;
            setPlain(*m, Pid::thr, -30.0f);
            bad += pm.modified() ? 0 : 1;
            const std::array<int, 3> factoryRows{ 0, fetRow, static_cast<int>(nBank) - 1 };
            std::array<std::optional<Preset>, 3> stored{};
            for (std::size_t k = 0; k < factoryRows.size(); ++k)
                stored[k] = store->get(bank[static_cast<std::size_t>(factoryRows[k])].uuid);
            const auto refused = [&](int index) { bad += call([&] { return pm.overwrite(index); }) ? 1 : 0; };
            for (const int k : factoryRows)
                refused(k);
            refused(-1);
            refused(pm.count());
            refused(std::numeric_limits<int>::max());
            refused(std::numeric_limits<int>::min());
            for (std::size_t k = 0; k < factoryRows.size(); ++k)
            {
                const std::optional<Preset> f = store->get(bank[static_cast<std::size_t>(factoryRows[k])].uuid);
                bad += f.has_value() && stored[k].has_value() && f->isFactory && sameContent(*f, *stored[k])
                               && f->modifiedMs == stored[k]->modifiedMs
                           ? 0 : 1;
            }
            bad += pm.modified() ? 0 : 1;
            P.eq("presets.overwrite.refused", bad, 0);
        }
        {
            // A store failure: another process deleted the row after this list was read (not yet polled). Refused:
            // the list, the selection and modified() stay, and nothing is re-created.
            std::int64_t bad = 0;
            const int rowN = [&pm, nBank] {
                for (int i = static_cast<int>(nBank); i < pm.count(); ++i)
                    if (pm.row(i).name == "Probe File No Mode")
                        return i;
                return -1;
            }();
            const juce::String uuidN = rowN >= 0 ? juce::String(pm.row(rowN).uuid) : juce::String();
            pm.revision();
            const std::vector<std::string> rows = listing();
            const int cur = pm.current();
            const bool mod = pm.modified();
            {
                fp::PresetStore other(cfg);                                     // another process's connection
                bad += rowN >= 0 && other.remove(uuidN) ? 0 : 1;
            }
            bad += rowN >= 0 && pm.overwrite(rowN) ? 1 : 0;
            bad += listing() == rows && pm.current() == cur && pm.modified() == mod ? 0 : 1;
            bad += store->get(uuidN).has_value() ? 1 : 0;
            pm.revision();                                                      // now the delete is seen
            bad += indexOfUuid(pm, uuidN) < 0 && pm.current() == indexOfUuid(pm, uuidC) ? 0 : 1;
            P.eq("presets.overwrite.store_failure", bad, 0);
        }

        P.eq("presets.manage.success_no_bump", successStill, 0);
        P.eq("presets.manage.refusal_changed", refusalMoved, 0);
        std::printf("NOTE     presets.manage: %lld successful calls, %lld refusals\n", static_cast<long long>(successes),
                    static_cast<long long>(refusals));
    }

    if (scratchDir.exists())
        scratchDir.deleteRecursively();
    return P.finish();
}
