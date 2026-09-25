// Source/plugin/Presets.cpp: the processor's PresetAccess over FunkPresets (02 §9.5; 01 §9.1-9.2; K2 #10, #19, #23;
// P3, S12). FunkGui v0.8.0's funkgui::presets as shipped (S12 lead revision 4): a PresetManager on the processor's
// APVTS with FCompressor's PresetHooks, the process-wide PresetStore (factory/FactoryBank.h), and the <PRESET> state
// hooks installed through PresetContext::stateHooks. Processor.cpp is untouched (SPRINTS §7 D20).
//
// Hooks (01 §9.2):
//   isPresetParameter  the 22 Mode-filtered parameters (kHostParams inPresets): never mode, extkey, listen, delta,
//                      bypass, quality or labudget (01 §3.1 rules)
//   beginApply         facade.beginBatch(): the audio thread keeps the previous BlockParams (K2 #23)
//   applyBefore        `mode` from the preset's modeId (registered, or retired -> its successor; missing or unknown ->
//                      clean); a plain Mode write, snapped on read like every other (K2 #4)
//   (PresetManager)    every preset parameter, absent or not finite -> its default; then the new baseline
//   onApplied          facade.endBatch(): the outermost close raises the engine snap after the last write
//   captureExtra       modeId and modeRev of the effective Mode (K2 #10)
//   findFactory        the compiled bank (a saved isFactory is trusted only while the bank still has the uuid)
//   initialPreset      the bank's Init: a fresh instance is Init, unmodified
//   mixParameter       empty: FCompressor has no load-time mix lock (Brickwall's mix is Mode-locked by the resolver)
//
// modified(): PresetManager::isModified() as shipped (half an interval in plain units per parameter; a continuous
// parameter: a millionth of its span), OR the effective Mode differs from the baseline's modeId. The Mode is not a
// preset parameter (the modeId attribute carries it), so without the second clause a Mode switch after a load would
// read as unmodified although it changes the sound. A state load without a <PRESET> child (a session saved before
// presets) gets an unnamed baseline holding the loaded Mode, so it is not "modified" either.
//
// The list (PresetAccess rows): the factory bank in bank order (Init at 0, then Modes.def slot order), then the user
// presets sorted by name (the store's Sort::name). Factory rows and values come from the compiled bank; user rows from
// the store, whose preset is re-read at apply (another process may have changed it). The store opens on the first
// PresetAccess call (only an editor makes one), then stays with this instance; revision() also polls it for another
// process's commits, and bumps on any list, selection or modified change. Message thread only (02 §9.5), except the
// state hooks, which run on whatever thread the host saves or loads state from and touch only the PresetManager
// (thread-safe for them: its current() is locked, the parameters are the processor's atomics).
//
// User-preset management (S12 lead revision 8; P3b, S12.4). Each call returns false and changes nothing when refused,
// and each success bumps revision() exactly once (announce()), whether or not the list itself changed:
//   rename(i, name)    user rows only; PresetStore::rename's rule (trimmed, not empty, not taken by any user or factory
//                      preset ignoring case; the preset may keep its own name). Renaming the current preset renames
//                      the identity the session saves (PresetManager::setCurrent: no parameter moves).
//   remove(i)          user rows only. Removing the current preset makes this instance untitled (current() -1): the
//                      identity goes, the baseline values and modeId stay, so no parameter moves and modified() reads
//                      as before. Another instance that had it loaded reads -1 by lookup (current()).
//   importFile(path)   PresetFile::read, then PresetStore::importPreset as shipped: a uuid the store lacks is kept (a
//                      shared session still names the preset), a known uuid with other content or a changed factory
//                      preset gets a fresh one, the name is made unique, tags and timestamps are the store's. A file
//                      identical to a preset already stored writes nothing and still succeeds (the preset is there).
//                      Refused: a relative path, an unreadable or foreign file (PresetFile), a newer format, and a
//                      modeId this build cannot resolve (a newer build's Mode; a file without one loads clean).
//   exportFile(i, p)   PresetFile::write of any row, factory rows included (the compiled bank's values; a user row:
//                      the store's current copy), to exactly `p` (the caller's save dialog owns the extension). A file
//                      never carries tags or timestamps. Refused: a relative path, a directory or unwritable location.
// Paths are absolute (a host's working directory means nothing to a plugin); "~" is expanded (juce::File).
#include "plugin/Processor.h"
#include "plugin/factory/FactoryBank.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/presets/PresetFile.h>
#include <funkgui/presets/PresetManager.h>
#include <funkgui/presets/PresetStore.h>
#include <funkgui/presets/PresetTypes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcmp
{
    namespace
    {
        using fcdsp::Pid;
        using funkgui::presets::Preset;
        using funkgui::presets::PresetHooks;
        using funkgui::presets::PresetManager;
        using funkgui::presets::PresetStore;

        constexpr std::string_view kFallbackKey = "clean";   // a preset without a usable modeId (01 §9.2)

        bool isPresetParameterId(const juce::String& id)
        {
            for (const fcdsp::HostParam& h : fcdsp::kHostParams)
                if (h.inPresets && id == h.id)
                    return true;
            return false;
        }

        // The Mode a preset loads (and the Mode a baseline stands for): its modeId, registered or retired (-> the
        // successor); a missing or unknown modeId -> clean. nullptr only for an empty registry.
        const fcdsp::ModeSlot* modeOf(const Preset& p)
        {
            const fcdsp::ModeSlot* ms = nullptr;
            if (const funkgui::presets::Attribute* a = p.attr(factory::kModeIdAttr))
                ms = fcdsp::resolveKey(a->value.toStdString());
            return ms != nullptr ? ms : fcdsp::resolveKey(kFallbackKey);
        }

        juce::String fromUtf8(std::string_view s)
        {
            return juce::String::fromUTF8(s.data(), static_cast<int>(s.size()));
        }

        // The file a PresetAccess path names: absolute only ("~" counts, juce::File expands it), else nullopt.
        std::optional<juce::File> fileAt(std::string_view path)
        {
            const juce::String p = fromUtf8(path);
            if (p.trim().isEmpty() || !juce::File::isAbsolutePath(p))
                return std::nullopt;
            return juce::File(p);
        }

        class Presets final : public PresetAccess
        {
        public:
            explicit Presets(const PresetContext& context)
                : facade_(context.facade),
                  stateHooks_(context.stateHooks),
                  mode_(context.apvts.getParameter(fcdsp::kHostParams[fcdsp::idx(Pid::mode)].id)),
                  manager_(context.apvts, makeHooks())
            {
                jassert(mode_ != nullptr);
                stateHooks_.writePreset = [this](juce::ValueTree& params) { manager_.writeState(params); };
                stateHooks_.readPreset = [this](juce::ValueTree& params) { readState(params); };
            }

            ~Presets() override
            {
                stateHooks_.writePreset = nullptr;             // the processor's StateHooks outlive this object
                stateHooks_.readPreset = nullptr;
            }

            Presets(const Presets&) = delete;
            Presets& operator=(const Presets&) = delete;

            int count() const override
            {
                refreshUsers();
                return static_cast<int>(factory::factoryBank().size() + users_.size());
            }

            Row row(int index) const override
            {
                if (index < 0 || index >= count())                // count() refreshes the user rows
                    return {};
                const std::vector<Preset>& bank = factory::factoryBank();
                const std::size_t i = static_cast<std::size_t>(index);
                const bool inBank = i < bank.size();
                const Preset& p = inBank ? bank[i] : users_[i - bank.size()];
                Row r;
                r.uuid = p.uuid.toStdString();
                r.name = p.name.toStdString();
                r.category = p.category.toStdString();
                if (const fcdsp::ModeSlot* ms = modeOf(p))
                    r.modeKey = std::string(ms->key);
                r.factory = inBank;
                return r;
            }

            int current() const override
            {
                const juce::String uuid = manager_.current().uuid;
                if (uuid.isEmpty())
                    return -1;                                    // untitled
                if (const int f = factory::factoryIndexOf(uuid); f >= 0)
                    return f;
                refreshUsers();
                for (std::size_t i = 0; i < users_.size(); ++i)
                    if (users_[i].uuid == uuid)
                        return static_cast<int>(factory::factoryBank().size() + i);
                return -1;                                        // deleted, or a preset this store never had
            }

            bool modified() const override
            {
                if (manager_.isModified())
                    return true;
                const fcdsp::ModeSlot* base = modeOf(manager_.current());
                return base != nullptr && base->slot != liveSlot();
            }

            uint32_t revision() const override
            {
                PresetStore& s = store();
                s.pollExternalChanges();
                const uint64_t storeRev = s.revision(), managerRev = manager_.revision();
                const bool mod = modified();
                if (storeRev != seenStore_ || managerRev != seenManager_ || mod != seenModified_)
                {
                    seenStore_ = storeRev;
                    seenManager_ = managerRev;
                    seenModified_ = mod;
                    ++revision_;
                }
                return revision_;
            }

            void apply(int index) override
            {
                const std::optional<Preset> p = presetAt(index);
                if (!p.has_value())
                    return;
                applyPreset(*p);
                store().markUsed(p->uuid);
            }

            void step(int delta) override
            {
                const int n = count();
                if (n == 0 || delta == 0)
                    return;
                const int at = current();
                const int from = at < 0 ? (delta > 0 ? -1 : 0) : at;
                apply(((from + delta) % n + n) % n);
            }

            bool saveAs(std::string_view name, std::string_view category) override
            {
                const juce::String n = fromUtf8(name).trim();
                if (n.isEmpty())
                    return false;
                Preset p = manager_.capture();                    // live values, modeId/modeRev (captureExtra)
                p.uuid = {};
                p.isFactory = false;
                p.tags = 0;
                p.author = {};
                p.notes = {};
                p.name = n;                                       // the store makes it unique ("Name 2")
                p.category = fromUtf8(category).trim();
                if (!store().saveNew(p))                          // assigns the uuid, the unique name, timestamps
                    return false;
                manager_.setCurrent(p);                           // the saved preset, unmodified
                return true;
            }

            bool rename(int index, std::string_view newName) override
            {
                const Preset* u = userAt(index);
                if (u == nullptr)
                    return false;                                 // a factory row, or no row
                const juce::String uuid = u->uuid;                // a copy: the rename refreshes users_
                const juce::String name = fromUtf8(newName).trim();
                if (!store().rename(uuid, name))                  // empty, taken, or gone (another process)
                    return false;
                Preset c = manager_.current();
                if (c.uuid == uuid)
                {
                    c.name = name;                                // the session saves the new name
                    manager_.setCurrent(c);
                }
                announce();
                return true;
            }

            bool remove(int index) override
            {
                const Preset* u = userAt(index);
                if (u == nullptr)
                    return false;
                const juce::String uuid = u->uuid;
                if (!store().remove(uuid))
                    return false;
                const Preset c = manager_.current();
                if (c.uuid == uuid)
                {
                    Preset untitled;                              // no identity; the same baseline and modeId
                    untitled.params = c.params;
                    untitled.attributes = c.attributes;
                    manager_.setCurrent(untitled);
                }
                announce();
                return true;
            }

            bool importFile(std::string_view path) override
            {
                const std::optional<juce::File> f = fileAt(path);
                if (!f.has_value())
                    return false;
                PresetStore& s = store();
                std::optional<Preset> p = funkgui::presets::PresetFile::read(s.config(), *f);
                if (!p.has_value())
                    return false;                                 // missing, too large, not XML, another product's
                if (const funkgui::presets::Attribute* a = p->attr(factory::kModeIdAttr);
                    a != nullptr && fcdsp::resolveKey(a->value.toStdString()) == nullptr)
                    return false;                                 // a Mode this build does not have
                if (!s.importPreset(std::move(*p)).ok)            // a newer format, a non-finite value, no database
                    return false;
                announce();
                return true;
            }

            bool exportFile(int index, std::string_view path) override
            {
                const std::optional<juce::File> f = fileAt(path);
                if (!f.has_value() || f->isDirectory())           // at once: juce::File's replace would retry a
                    return false;                                 // directory for half a second, then fail
                std::optional<Preset> p = presetAt(index);        // nullopt: no row, or deleted by another process
                if (!p.has_value())
                    return false;
                p->tags = 0;                                      // personal: PresetFile never writes these, and
                p->createdMs = p->modifiedMs = p->lastUsedMs = 0; // nothing personal leaves the copy either
                if (!funkgui::presets::PresetFile::write(store().config(), *p, *f))
                    return false;
                announce();
                return true;
            }

        private:
            PresetHooks makeHooks()
            {
                PresetHooks h;
                h.isPresetParameter = &isPresetParameterId;
                h.beginApply = [this] {
                    facade_.beginBatch();
                    applying_ = true;
                };
                h.applyBefore = [this](const Preset& p) { writeMode(p); };
                h.onApplied = [this] {
                    applying_ = false;
                    facade_.endBatch();
                };
                h.captureExtra = [this](Preset& p) { setModeAttributes(p); };
                h.findFactory = [](const juce::String& uuid) { return factory::findFactory(uuid); };
                h.initialPreset = [] { return factory::factoryBank().front(); };
                return h;
            }

            // The effective slot of the `mode` parameter's raw value (resolveSlot: retired -> successor).
            int liveSlot() const { return facade_.currentRaw().modeSlot; }

            void setModeAttributes(Preset& p) const
            {
                const fcdsp::ModeSlot& ms = fcdsp::resolveSlot(liveSlot());
                if (ms.entry == nullptr || ms.entry->desc == nullptr)
                    return;                                       // an empty registry: nothing to name
                p.setAttr(factory::kModeIdAttr, fromUtf8(ms.key));
                p.setAttr(factory::kModeRevAttr, juce::String(static_cast<int>(ms.entry->desc->revision)));
            }

            void writeMode(const Preset& p)
            {
                const fcdsp::ModeSlot* ms = modeOf(p);
                if (ms == nullptr || mode_ == nullptr)
                    return;
                const float norm = mode_->convertTo0to1(static_cast<float>(ms->slot));
                if (mode_->getValue() != norm || liveSlot() != ms->slot)
                    mode_->setValueNotifyingHost(norm);
            }

            // PresetManager::apply inside the batch its hooks open; the batch is closed even if the apply throws, so
            // the audio thread is never left on the previous BlockParams for good (K2 #23).
            void applyPreset(const Preset& p)
            {
                try
                {
                    manager_.apply(p);
                }
                catch (...)
                {
                    if (applying_)
                    {
                        applying_ = false;
                        facade_.endBatch();
                    }
                    throw;
                }
            }

            // The <PRESET> child (inside the state load's batch): identity and baseline without touching a
            // parameter. A session without one gets an unnamed baseline of the loaded values; either way the
            // baseline names the loaded Mode when the child does not.
            void readState(juce::ValueTree& params)
            {
                manager_.readState(params);
                Preset c = manager_.current();
                if (c.attr(factory::kModeIdAttr) == nullptr)
                {
                    setModeAttributes(c);
                    manager_.setCurrent(c);
                }
            }

            PresetStore& store() const
            {
                if (!store_.has_value())
                    store_.emplace();                             // opens the database (and syncs the factory rows)
                return store_->getObject();
            }

            void refreshUsers() const
            {
                PresetStore& s = store();
                if (usersValid_ && usersRev_ == s.revision())
                    return;
                funkgui::presets::Query q;
                q.source = funkgui::presets::Source::user;
                q.sort = funkgui::presets::Sort::name;
                users_ = s.query(q);
                usersRev_ = s.revision();
                usersValid_ = true;
            }

            // The preset behind a row: the compiled bank for a factory row, the store's current copy for a user row.
            std::optional<Preset> presetAt(int index) const
            {
                const std::vector<Preset>& bank = factory::factoryBank();
                if (index < 0 || index >= count())
                    return std::nullopt;
                const std::size_t i = static_cast<std::size_t>(index);
                if (i < bank.size())
                    return bank[i];
                return store().get(users_[i - bank.size()].uuid);    // nullopt: deleted by another process
            }

            // The listed user preset behind a row (valid until the list next refreshes), or nullptr: a factory row, or
            // out of range.
            const Preset* userAt(int index) const
            {
                if (index < 0 || index >= count())                // count() refreshes the user rows
                    return nullptr;
                const std::size_t i = static_cast<std::size_t>(index);
                const std::size_t nBank = factory::factoryBank().size();
                return i < nBank ? nullptr : &users_[i - nBank];
            }

            // A successful management call: exactly one bump of revision(), which takes the store's and the manager's
            // revisions and modified() as seen, so the next poll does not bump a second time for the same call. A call
            // that changed no list (an export, a duplicate import) still bumps: the browser redraws on every success.
            void announce()
            {
                seenStore_ = store().revision();
                seenManager_ = manager_.revision();
                seenModified_ = modified();
                ++revision_;
            }

            ProcessorFacade& facade_;
            StateHooks& stateHooks_;
            juce::RangedAudioParameter* mode_;
            bool applying_ = false;                               // between beginApply and onApplied
            PresetManager manager_;                               // last of the members its hooks use

            mutable std::optional<juce::SharedResourcePointer<factory::SharedPresetStore>> store_;
            mutable std::vector<Preset> users_;                   // user presets, by name
            mutable uint64_t usersRev_ = 0;
            mutable bool usersValid_ = false;
            mutable uint64_t seenStore_ = 0, seenManager_ = 0;
            mutable bool seenModified_ = false;
            mutable uint32_t revision_ = 1;
        };
    } // namespace

    std::unique_ptr<PresetAccess> makePresetAccess(const PresetContext& context)
    {
        return std::make_unique<Presets>(context);
    }
} // namespace fcmp
