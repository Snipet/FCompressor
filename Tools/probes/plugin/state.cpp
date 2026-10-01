// FCMP_PROBE layer=proc name=state scope=mode timeout=120
//
// proc.state.<key> (P2, S8; 03 §3.5; 01 §9.1; C §5.7.1-2; K2 #10, #23, #25c; K3 #14; HR StateProbe :357-445): session
// state through the processor's own get/setStateInformation, in this Mode.
//
// Instance A: this Mode, each of the 30 host parameters at a distinct odd value written the way a host or the UI writes
// one (the normalised value of: a non-default step of a stepped/hybrid active spec, i.e. the UI's snap on write; an
// irrational fraction of the Mode's range for a live parameter, of the host range for a locked/derived/n/a one; odd
// switch positions; listen and delta ON; quality HQ; lookahead budget 20 MS) and UiState {expanded, colour}; its blob is
// A.getStateInformation. Every instance a blob is loaded into is NOT fresh: another Mode, every parameter at other odd
// values, the other UiState, prepared at 48 kHz and processing noise (a running instance).
//
// Rows (spec; "bitwise" compares the raw plain value: the APVTS atomic the processor reads, which is the truth, 01 §1.3):
//   state.xml.*                   A's blob: <PARAMS stateVersion="1" modeId=<key> modeRev=<revision> product build>, the
//                                 30 PARAMs in kApvtsOrder with A's raw values, <UI charExpanded="1" scTab="colour"/>,
//                                 at most one <PRESET> (P3's hook; proc.presets checks it), no other child
//   state.restore.raw.mismatches  B vs A after B loads the blob, the 27 parameters other than listen/delta, bitwise: 0
//   state.restore.monitoring      B's listen and delta are exactly 0 although A saved them ON (K2 #25c): 0 wrong
//   state.restore.mode            B's effective slot is this Mode's (the Mode travels as its key): 1
//   state.restore.norm.max_err    max |B.getValue() - A.getValue()| over the 27: <= 1e-6 (the host map uses libm, K2 #14)
//   state.restore.text.mismatches the host text of the 27 in B equals A's, once SetupWatcher has applied the loaded
//                                 quality/labudget to both (the `look` text reads the configured budget): 0
//   state.restore.ui              B.uiState() == A.uiState(): 1
//   state.restore.notice          serial + 1, every flag false, both keys empty: 0 wrong
//   state.restore.running         B keeps processing after the load: non-finite output samples 0
//   state.odd.raw.mismatches      A's blob with every continuous value moved to the next float (values the host map need
//                                 not produce), loaded into non-fresh D: D holds exactly those floats: 0
//   state.odd.resave.mismatches   D's own save holds the same floats: 0
//   state.absent.mismatches       A's blob without the PARAMs at even host positions (`mode` among them; modeId kept)
//                                 and without `output` (absent from every v1.0/v1.1 session, ADR-88), loaded into
//                                 non-fresh E (its OUTPUT away from 0 dB): removed -> the table default, bitwise (a
//                                 fresh instance's exact defaults), `mode` -> this Mode's slot, kept -> A, bitwise: 0
//   state.absent.norm.max_err     removed parameters: max |getValue() - getDefaultValue()| <= 1e-6
//   state.idempotent              save(load(save(B))) through non-fresh C is byte-identical to save(B): 1
//   state.concurrent.*            41 loads alternating A's blob and G's own into running G while a second thread calls
//                                 processBlock continuously (at least 2 blocks between loads): the last (A's) restores
//                                 bitwise, no non-finite output, no batch left open: 0
//   state.race.*                  (S13 H1b, lead revision 5a: the S8 race) a host thread loads R 200 times, alternating
//                                 A's blob and M (A's with an unknown modeId and UiState {expanded, sidechain}), while a
//                                 second host thread saves R continuously and this thread, the message thread, plays
//                                 the editor: it reads stateNotice() and uiState() and clicks the SC|COLOUR tab through
//                                 the reference. Every notice read is one a load published (flags and keys agree: 0
//                                 torn) and serials never go back (0); every concurrent save is a <PARAMS> tree with a
//                                 <UI> child (0 bad); a final load from another thread reaches the editor's copy at its
//                                 next uiState() (1) with serial = loads + 1 and A's notice (0 wrong). Under tsan-agent
//                                 a data race on the notice or the UI state fails the probe (the S8 finding)
//   state.batch.*                 fcmp::loadState into non-fresh F through a recording facade: exactly one beginBatch
//                                 (the first event) and one endBatch (the last), every parameter change and the readPreset
//                                 hook between them (so endBatch's snap follows the last write, K2 #23), depth 0 after
//   state.hooks.*                 fcmp::saveState with a writePreset hook: the hook sees the 30 PARAMs and no <UI>, and
//                                 its <PRESET> is saved between them; readPreset gets the tree with that <PRESET>
// NOTE: how many of the odd values are not host-map fixed points (toPlain(toNorm(v)) != v, i.e. would not survive a
// restore through the normalised value alone).
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Signals.h"

#include "plugin/Processor.h"
#include "plugin/State.h"

#include "FcmpProduct.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using fcdsp::HostParam;
    using fcdsp::Kind;
    using fcdsp::Map;
    using fcdsp::Pid;
    using funkgui::test::Probe;
    namespace sig = fcmp::probe::sig;

    using Values = std::array<float, fcdsp::kNumParams>;         // Pid order

    constexpr double kFs = 48000.0;
    constexpr int kBlock = 256;

    std::uint32_t bitsOf(float v)
    {
        std::uint32_t b = 0;
        std::memcpy(&b, &v, sizeof b);
        return b;
    }

    bool isMonitoring(Pid p) { return p == Pid::listen || p == Pid::delta; }
    bool isContinuousMap(Map m) { return m == Map::linear || m == Map::log || m == Map::power || m == Map::ratio3; }

    // An irrational-step sequence in (0.07, 0.93): distinct for every salt, never a range end or a midpoint.
    float frac(int salt)
    {
        const double f = 0.2360679774997897 + 0.6180339887498949 * static_cast<double>(salt);
        return static_cast<float>(0.07 + 0.86 * (f - std::floor(f)));
    }

    void setPlain(fcmp::Processor& proc, Pid pid, float plain)
    {
        juce::RangedAudioParameter& p = proc.parameter(pid);
        p.setValueNotifyingHost(p.convertTo0to1(plain));
    }

    Values rawValues(const fcmp::Processor& proc)
    {
        Values v{};
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            v[i] = proc.rawValue(static_cast<Pid>(i));
        return v;
    }

    // A distinct odd value for a Mode-filtered parameter under its active spec (see the header).
    float oddModeValue(const fcdsp::ParamSpec* spec, const HostParam& h, int salt, float current)
    {
        const float f = frac(salt);
        if (h.map == Map::boolean)
            return 0.3f + 0.4f * f;                              // a switch position, ON or OFF
        if (spec != nullptr && (spec->kind == Kind::stepped || spec->kind == Kind::hybrid) && !spec->steps.empty())
        {
            const std::size_t n = spec->steps.size();
            std::size_t k = static_cast<std::size_t>(salt) % n;
            if (n > 1 && spec->steps[k].plain == current)
                k = (k + 1) % n;
            return spec->steps[k].plain;
        }
        if (spec != nullptr && spec->kind == Kind::continuous && spec->lo < spec->hi)
            return spec->lo + (spec->hi - spec->lo) * f;
        if (h.map == Map::index)
            return static_cast<float>(1 + salt % (h.numSteps - 1));
        return h.lo + (h.hi - h.lo) * f;
    }

    // `en` at odd values (salted), in kResolveOrder so each value is chosen under the spec its drivers select; then the
    // globals and the UI state. One batch, like a preset or a UI multi-write.
    struct Globals
    {
        float extkey, listen, delta, bypass, quality, labudget;
        fcmp::UiState ui;
        float output;                                            // v1.2 (ADR-88), dB
    };

    void setOdd(fcmp::Processor& proc, const fcdsp::ModeEntry& en, int salt, const Globals& g)
    {
        fcdsp::RawParams raw = fcmp::probe::modeRaw(en);
        proc.beginBatch();
        setPlain(proc, Pid::mode, static_cast<float>(fcdsp::slotOf(en)));
        for (const Pid pid : fcdsp::kResolveOrder)
        {
            fcdsp::ParamView view;
            fcdsp::resolveView(*en.desc, raw, view);
            const std::size_t i = fcdsp::idx(pid);
            raw.v[i] = oddModeValue(view.spec[i], fcdsp::kHostParams[i], salt + static_cast<int>(i), raw.v[i]);
            setPlain(proc, pid, raw.v[i]);
        }
        setPlain(proc, Pid::extkey, g.extkey);
        setPlain(proc, Pid::listen, g.listen);
        setPlain(proc, Pid::delta, g.delta);
        setPlain(proc, Pid::bypass, g.bypass);
        setPlain(proc, Pid::quality, g.quality);
        setPlain(proc, Pid::labudget, g.labudget);
        setPlain(proc, Pid::output, g.output);
        proc.endBatch();
        proc.uiState() = g.ui;
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

    // A running instance in another Mode, every parameter elsewhere (the receiving end of every load here).
    std::unique_ptr<fcmp::Processor> nonFresh(const fcdsp::ModeEntry& other, int salt)
    {
        auto proc = std::make_unique<fcmp::Processor>();
        setOdd(*proc, other, salt, Globals{ 0.13f, 0.0f, 0.0f, 0.64f, 0.0f, 1.0f, fcmp::UiState{}, 5.3f });
        proc->prepareToPlay(kFs, kBlock);
        run(*proc, 8, 0xabcdu + static_cast<std::uint64_t>(salt));
        return proc;
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

    juce::XmlElement* paramChild(juce::XmlElement& root, const char* id)
    {
        for (juce::XmlElement* e : root.getChildWithTagNameIterator("PARAM"))
            if (e->getStringAttribute("id") == id)
                return e;
        return nullptr;
    }

    // The raw values a blob stores, Pid order (NaN when a parameter is missing).
    Values savedValues(const juce::MemoryBlock& blob)
    {
        Values v{};
        v.fill(std::nanf(""));
        if (const std::unique_ptr<juce::XmlElement> xml = xmlOf(blob))
            for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
                if (const juce::XmlElement* e = paramChild(*xml, fcdsp::kHostParams[i].id))
                    v[i] = static_cast<float>(e->getDoubleAttribute("value"));
        return v;
    }

    std::string fmt9(float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));
        return buf;
    }

    // ---- the batch recorder -------------------------------------------------------------------------------------------
    struct Event
    {
        enum Kind : std::uint8_t { begin, end, write, readPreset } kind;
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
} // namespace

FCMP_PROBE(proc, state)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    const fcdsp::ModeEntry& en = fcmp::probe::modeEntry(C.key);
    const int slot = fcdsp::slotOf(en);
    const std::span<const fcdsp::ModeSlot> slots = fcdsp::modeSlots();
    const fcdsp::ModeEntry* other = nullptr;                     // the next registered Mode: every receiver's Mode
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (slots[i].slot == slot)
            other = slots[(i + 1) % slots.size()].entry;
    if (other == nullptr || other == &en)
        other = &en;                                             // a one-Mode registry: still non-fresh values

    // ---- A and its blob -----------------------------------------------------------------------------------------------
    const fcmp::UiState uiA{ true, fcmp::ScTab::colour };
    auto a = std::make_unique<fcmp::Processor>();
    setOdd(*a, en, 1, Globals{ 0.61f, 1.0f, 0.77f, 0.29f, 2.0f, 2.0f, uiA, -3.7f });
    a->setupWatcher().poll();                                    // applies quality/labudget (the `look` text reads the
                                                                 // configured budget), as the 20 Hz timer would
    const Values rawA = rawValues(*a);
    const juce::MemoryBlock blob = save(*a);

    {
        const std::unique_ptr<juce::XmlElement> xml = xmlOf(blob);
        P.eq("state.xml.root", xml != nullptr && xml->hasTagName("PARAMS") ? 1 : 0, 1);
        if (xml == nullptr)
            return P.finish();
        const juce::String key = juce::String::fromUTF8(C.key.data(), static_cast<int>(C.key.size()));
        P.eq("state.xml.version", xml->getStringAttribute("stateVersion") == juce::String(fcdsp::kStateVersion) ? 1 : 0,
             1);
        P.eq("state.xml.modeid", xml->getStringAttribute("modeId") == key ? 1 : 0, 1);
        P.eq("state.xml.moderev", xml->getIntAttribute("modeRev", -1), static_cast<std::int64_t>(en.desc->revision));
        P.eq("state.xml.product", xml->getStringAttribute("product") == fcmp::product::kName
                                      && xml->getStringAttribute("build") == fcmp::product::kVersion ? 1 : 0, 1);
        std::int64_t params = 0, order = 0, valueBad = 0, others = 0;
        const juce::XmlElement* ui = nullptr;
        bool preset = false;
        for (const juce::XmlElement* e : xml->getChildIterator())
        {
            if (e->hasTagName("PARAM"))
            {
                const std::size_t pos = static_cast<std::size_t>(params);
                if (pos >= fcdsp::kNumParams)
                    ++order;
                else
                {
                    const HostParam& h = fcdsp::kHostParams[fcdsp::idx(fcdsp::kApvtsOrder[pos])];
                    order += e->getStringAttribute("id") == h.id ? 0 : 1;
                    const float v = static_cast<float>(e->getDoubleAttribute("value", -1e30));
                    valueBad += bitsOf(v) == bitsOf(rawA[fcdsp::idx(h.pid)]) ? 0 : 1;
                }
                ++params;
            }
            else if (e->hasTagName("UI") && ui == nullptr)
                ui = e;
            else if (e->hasTagName("PRESET") && !preset)         // P3's <PRESET> hook (01 §9.1 save step 3)
                preset = true;
            else
                ++others;
        }
        P.eq("state.xml.params", params, static_cast<std::int64_t>(fcdsp::kNumParams));
        P.eq("state.xml.order.mismatches", order, 0);
        P.eq("state.xml.values.mismatches", valueBad, 0);
        P.eq("state.xml.ui", ui != nullptr && ui->getStringAttribute("charExpanded") == "1"
                                 && ui->getStringAttribute("scTab") == "colour" ? 1 : 0, 1);
        P.eq("state.xml.other_children", others, 0);
    }

    // ---- restore into a running instance in another Mode --------------------------------------------------------------
    auto b = nonFresh(*other, 40);
    const fcmp::StateNotice before = b->stateNotice();
    load(*b, blob);
    b->setupWatcher().poll();                                    // the loaded quality/labudget reconfigure B
    {
        const Values rawB = rawValues(*b);
        std::int64_t rawBad = 0, textBad = 0, monitoring = 0;
        double normErr = 0.0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const Pid pid = static_cast<Pid>(i);
            if (isMonitoring(pid))
            {
                monitoring += bitsOf(rawB[i]) == bitsOf(0.0f) ? 0 : 1;
                continue;
            }
            if (bitsOf(rawB[i]) != bitsOf(rawA[i]))
            {
                if (rawBad < 4)
                    std::printf("NOTE     state.restore %s: saved %.9g, restored %.9g\n", fcdsp::kHostParams[i].id,
                                static_cast<double>(rawA[i]), static_cast<double>(rawB[i]));
                ++rawBad;
            }
            const juce::RangedAudioParameter& pa = a->parameter(pid);
            const juce::RangedAudioParameter& pb = b->parameter(pid);
            normErr = std::max(normErr, std::abs(static_cast<double>(pb.getValue()) - static_cast<double>(pa.getValue())));
            if (pa.getCurrentValueAsText() != pb.getCurrentValueAsText())
            {
                std::printf("NOTE     state.restore.text %s: '%s' vs '%s'\n", fcdsp::kHostParams[i].id,
                            pa.getCurrentValueAsText().toRawUTF8(), pb.getCurrentValueAsText().toRawUTF8());
                ++textBad;
            }
        }
        P.eq("state.restore.raw.mismatches", rawBad, 0);
        P.eq("state.restore.monitoring", monitoring, 0);
        P.eq("state.restore.mode", b->currentRaw().modeSlot == slot ? 1 : 0, 1);
        P.le("state.restore.norm.max_err", normErr, 1e-6);
        P.eq("state.restore.text.mismatches", textBad, 0);
        const fcmp::UiState& ub = b->uiState();
        P.eq("state.restore.ui", ub.charExpanded == uiA.charExpanded && ub.scTab == uiA.scTab ? 1 : 0, 1);
        const fcmp::StateNotice n = b->stateNotice();
        const std::int64_t noticeBad = (n.serial == before.serial + 1u ? 0 : 1) + (n.newerSession ? 1 : 0)
                                     + (n.modeMigrated ? 1 : 0) + (n.modeRevised ? 1 : 0) + (n.fromKey[0] != '\0' ? 1 : 0)
                                     + (n.toKey[0] != '\0' ? 1 : 0);
        P.eq("state.restore.notice", noticeBad, 0);
        P.eq("state.restore.running", run(*b, 8, 0x77u), 0);
    }

    // ---- odd values the host map need not produce ----------------------------------------------------------------------
    {
        const std::unique_ptr<juce::XmlElement> xml = xmlOf(blob);
        Values want = rawA;
        std::int64_t nonFixed = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const HostParam& h = fcdsp::kHostParams[i];
            if (!isContinuousMap(h.map))
                continue;
            float v = std::nextafter(rawA[i], h.hi);
            if (v == rawA[i])
                v = std::nextafter(rawA[i], h.lo);
            want[i] = v;
            nonFixed += fcdsp::toPlain(h.pid, fcdsp::toNorm(h.pid, v)) != v ? 1 : 0;
            if (juce::XmlElement* e = paramChild(*xml, h.id))
                e->setAttribute("value", juce::String(fmt9(v)));
        }
        want[fcdsp::idx(Pid::listen)] = 0.0f;
        want[fcdsp::idx(Pid::delta)] = 0.0f;
        auto d = nonFresh(*other, 50);
        load(*d, blobOf(*xml));
        const Values got = rawValues(*d);
        const Values resaved = savedValues(save(*d));
        std::int64_t bad = 0, resaveBad = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            bad += bitsOf(got[i]) == bitsOf(want[i]) ? 0 : 1;
            resaveBad += bitsOf(resaved[i]) == bitsOf(want[i]) ? 0 : 1;
        }
        P.eq("state.odd.raw.mismatches", bad, 0);
        P.eq("state.odd.resave.mismatches", resaveBad, 0);
        std::printf("NOTE     state.odd: %lld of the continuous values are not host-map fixed points\n",
                    static_cast<long long>(nonFixed));
    }

    // ---- absent children ---------------------------------------------------------------------------------------------
    {
        const std::unique_ptr<juce::XmlElement> xml = xmlOf(blob);
        std::array<bool, fcdsp::kNumParams> removed{};
        for (std::size_t pos = 0; pos < fcdsp::kNumParams; pos += 2)
        {
            const HostParam& h = fcdsp::kHostParams[fcdsp::idx(fcdsp::kApvtsOrder[pos])];
            if (juce::XmlElement* e = paramChild(*xml, h.id))
                xml->removeChildElement(e, true);
            removed[fcdsp::idx(h.pid)] = true;
        }
        if (juce::XmlElement* o = paramChild(*xml, fcdsp::kHostParams[fcdsp::idx(Pid::output)].id))
            xml->removeChildElement(o, true);                    // ADR-88: a v1.0/v1.1 session has no `output`
        removed[fcdsp::idx(Pid::output)] = true;
        auto e = nonFresh(*other, 60);
        load(*e, blobOf(*xml));
        std::int64_t bad = 0;
        double normErr = 0.0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
        {
            const Pid pid = static_cast<Pid>(i);
            const HostParam& h = fcdsp::kHostParams[i];
            float want = removed[i] ? h.def : rawA[i];
            if (pid == Pid::mode)
                want = static_cast<float>(slot);                 // modeId wins over the (absent) slot
            else if (isMonitoring(pid))
                want = 0.0f;
            const float got = e->rawValue(pid);
            if (bitsOf(got) != bitsOf(want))
            {
                std::printf("NOTE     state.absent %s: want %.9g got %.9g\n", h.id, static_cast<double>(want),
                            static_cast<double>(got));
                ++bad;
            }
            if (removed[i] && pid != Pid::mode)
            {
                const juce::RangedAudioParameter& p = e->parameter(pid);
                normErr = std::max(normErr, std::abs(static_cast<double>(p.getValue())
                                                     - static_cast<double>(p.getDefaultValue())));
            }
        }
        P.eq("state.absent.mismatches", bad, 0);
        P.le("state.absent.norm.max_err", normErr, 1e-6);
    }

    // ---- save(load(save)) is a fixed point ---------------------------------------------------------------------------
    {
        const juce::MemoryBlock blobB = save(*b);
        auto c = nonFresh(*other, 70);
        load(*c, blobB);
        P.eq("state.idempotent", save(*c) == blobB ? 1 : 0, 1);
    }

    // ---- loads while an audio thread processes ------------------------------------------------------------------------
    {
        auto g = nonFresh(*other, 90);
        const juce::MemoryBlock blobOther = save(*g);            // G's own session, alternated with A's
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
        for (int i = 0; i <= 40; ++i)                            // ends with A's blob
        {
            load(*g, i % 2 == 0 ? blob : blobOther);
            const std::int64_t b0 = blocks.load(std::memory_order_acquire);
            while (blocks.load(std::memory_order_acquire) < b0 + 2)   // the audio thread runs between loads
                std::this_thread::yield();
        }
        stop.store(true, std::memory_order_release);
        audio.join();
        const Values rawG = rawValues(*g);
        std::int64_t bad = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            bad += bitsOf(rawG[i]) == bitsOf(isMonitoring(static_cast<Pid>(i)) ? 0.0f : rawA[i]) ? 0 : 1;
        P.eq("state.concurrent.raw.mismatches", bad, 0);
        P.eq("state.concurrent.nonfinite", nonfinite.load(), 0);
        P.eq("state.concurrent.batch_open", g->batchDepth(), 0);
        std::printf("NOTE     state.concurrent: 41 loads, %lld blocks processed meanwhile\n",
                    static_cast<long long>(blocks.load()));
    }

    // ---- loads and saves on host threads while the editor reads the notice and the UI state (lead revision 5a) -------
    {
        constexpr int kRaceLoads = 200;
        constexpr const char* kUnknownKey = "zz-race";           // not a registered key: the load migrates to clean
        juce::MemoryBlock blobM;
        {
            const std::unique_ptr<juce::XmlElement> xml = xmlOf(blob);
            xml->setAttribute("modeId", kUnknownKey);
            if (juce::XmlElement* ui = xml->getChildByName("UI"))
                ui->setAttribute("scTab", "sidechain");
            blobM = blobOf(*xml);
        }
        // A notice some load published: A's (nothing to report) or M's (migrated from kUnknownKey to clean).
        const auto consistent = [&](const fcmp::StateNotice& n) {
            const bool plain = !n.modeMigrated && n.fromKey[0] == '\0' && n.toKey[0] == '\0';
            const bool migrated = n.modeMigrated && std::strcmp(n.fromKey, kUnknownKey) == 0
                               && std::strcmp(n.toKey, "clean") == 0;
            return !n.newerSession && !n.modeRevised && (plain || migrated || n.serial == 0u);
        };

        auto r = nonFresh(*other, 100);
        const std::uint32_t serial0 = r->stateNotice().serial;
        std::atomic<bool> loading{ true };
        std::atomic<std::int64_t> saves{ 0 }, badSaves{ 0 };
        std::thread host([&] {
            for (int i = 0; i < kRaceLoads; ++i)
                load(*r, i % 2 == 0 ? blobM : blob);
            loading.store(false, std::memory_order_release);
        });
        std::thread saver([&] {
            while (loading.load(std::memory_order_acquire) || saves.load(std::memory_order_relaxed) == 0)
            {
                const std::unique_ptr<juce::XmlElement> xml = xmlOf(save(*r));
                const juce::XmlElement* ui = xml != nullptr ? xml->getChildByName("UI") : nullptr;
                const bool ok = xml != nullptr && xml->hasTagName("PARAMS") && ui != nullptr
                             && (ui->getStringAttribute("charExpanded") == "0"
                                 || ui->getStringAttribute("charExpanded") == "1")
                             && (ui->getStringAttribute("scTab") == "sidechain"
                                 || ui->getStringAttribute("scTab") == "colour");
                badSaves.fetch_add(ok ? 0 : 1, std::memory_order_relaxed);
                saves.fetch_add(1, std::memory_order_relaxed);
            }
        });
        std::int64_t reads = 0, torn = 0, backwards = 0;
        std::uint32_t last = serial0;
        while (loading.load(std::memory_order_acquire))
        {
            const fcmp::StateNotice n = r->stateNotice();
            torn += consistent(n) ? 0 : 1;
            backwards += n.serial < last ? 1 : 0;
            last = n.serial;
            fcmp::UiState& ui = r->uiState();                    // the editor: read, then a tab click now and then
            if (++reads % 7 == 0)
                ui.scTab = ui.scTab == fcmp::ScTab::colour ? fcmp::ScTab::sidechain : fcmp::ScTab::colour;
        }
        host.join();
        saver.join();
        std::thread lastLoad([&] { load(*r, blob); });           // one more load, off this thread, with A's UI
        lastLoad.join();
        const fcmp::UiState& ui = r->uiState();
        const fcmp::StateNotice n = r->stateNotice();
        P.eq("state.race.notice.torn", torn, 0);
        P.eq("state.race.notice.backwards", backwards, 0);
        P.eq("state.race.save.bad", badSaves.load(), 0);
        P.eq("state.race.ui.final", ui.charExpanded == uiA.charExpanded && ui.scTab == uiA.scTab ? 1 : 0, 1);
        P.eq("state.race.notice.final", (n.serial == serial0 + kRaceLoads + 1u ? 0 : 1) + (n.modeMigrated ? 1 : 0)
                                            + (n.fromKey[0] != '\0' ? 1 : 0) + (n.toKey[0] != '\0' ? 1 : 0), 0);
        std::printf("NOTE     state.race: %d loads, %lld saves and %lld editor reads overlapped\n", kRaceLoads + 1,
                    static_cast<long long>(saves.load()), static_cast<long long>(reads));
    }

    // ---- one batch, the hooks ----------------------------------------------------------------------------------------
    {
        // saveState with a writePreset hook (P3's seam): the hook runs after the PARAMs and before <UI>.
        std::int64_t hookParams = -1, hookUi = -1;
        fcmp::StateHooks writeHooks;
        writeHooks.writePreset = [&](juce::ValueTree& t) {
            hookParams = 0;
            hookUi = 0;
            for (int c = 0; c < t.getNumChildren(); ++c)
            {
                hookParams += t.getChild(c).hasType("PARAM") ? 1 : 0;
                hookUi += t.getChild(c).hasType("UI") ? 1 : 0;
            }
            juce::ValueTree preset("PRESET");
            preset.setProperty("uuid", "00000000-0000-4000-8000-000000000001", nullptr);
            t.appendChild(preset, nullptr);
        };
        fcmp::StateNotice scratchNotice;
        juce::MemoryBlock hooked;
        fcmp::saveState(fcmp::StateContext{ a->apvts(), *a, a->uiState(), scratchNotice, writeHooks }, hooked);
        const std::unique_ptr<juce::XmlElement> xml = xmlOf(hooked);
        juce::StringArray tags;
        if (xml != nullptr)
            for (const juce::XmlElement* e : xml->getChildIterator())
                tags.add(e->getTagName());
        const int presetAt = tags.indexOf("PRESET");
        P.eq("state.hooks.write_sees", hookParams == static_cast<std::int64_t>(fcdsp::kNumParams) && hookUi == 0 ? 1 : 0,
             1);
        P.eq("state.hooks.write_order", presetAt == static_cast<int>(fcdsp::kNumParams)
                                            && tags.indexOf("UI") == presetAt + 1 && tags.size() == presetAt + 2 ? 1 : 0,
             1);

        // loadState through a recording facade, with a readPreset hook.
        auto f = nonFresh(*other, 80);
        std::vector<Event> log;
        RecordingFacade facade(*f, log);
        WriteRecorder writes(*f, log);
        for (juce::AudioProcessorParameter* p : f->getParameters())
            p->addListener(&writes);
        std::int64_t readSeesPreset = 0;
        fcmp::StateHooks readHooks;
        readHooks.readPreset = [&](juce::ValueTree& t) {
            log.push_back({ Event::readPreset, f->batchDepth() });
            readSeesPreset = t.getChildWithName("PRESET").isValid() ? 1 : 0;
        };
        fcmp::StateNotice notice;
        fcmp::loadState(fcmp::StateContext{ f->apvts(), facade, f->uiState(), notice, readHooks }, hooked.getData(),
                        static_cast<int>(hooked.getSize()));
        for (juce::AudioProcessorParameter* p : f->getParameters())
            p->removeListener(&writes);

        std::int64_t begins = 0, ends = 0, writesIn = 0, writesOut = 0, reads = 0, readsIn = 0;
        for (const Event& ev : log)
        {
            begins += ev.kind == Event::begin ? 1 : 0;
            ends += ev.kind == Event::end ? 1 : 0;
            if (ev.kind == Event::write)
                (ev.depth >= 1 ? writesIn : writesOut) += 1;
            if (ev.kind == Event::readPreset)
            {
                ++reads;
                readsIn += ev.depth >= 1 ? 1 : 0;
            }
        }
        P.eq("state.batch.begins", begins, 1);
        P.eq("state.batch.ends", ends, 1);
        P.eq("state.batch.first_is_begin", !log.empty() && log.front().kind == Event::begin ? 1 : 0, 1);
        P.eq("state.batch.last_is_end", !log.empty() && log.back().kind == Event::end ? 1 : 0, 1);
        P.eq("state.batch.writes_outside", writesOut, 0);
        P.ge("state.batch.writes_inside", static_cast<double>(writesIn), 1.0);
        P.eq("state.batch.depth_after", f->batchDepth(), 0);
        P.eq("state.hooks.read_in_batch", reads == 1 && readsIn == 1 ? 1 : 0, 1);
        P.eq("state.hooks.read_sees_preset", readSeesPreset, 1);
        const Values rawF = rawValues(*f);
        std::int64_t bad = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            bad += bitsOf(rawF[i]) == bitsOf(isMonitoring(static_cast<Pid>(i)) ? 0.0f : rawA[i]) ? 0 : 1;
        P.eq("state.hooks.restore.mismatches", bad, 0);
        std::printf("NOTE     state.batch: %lld parameter changes inside the batch\n", static_cast<long long>(writesIn));
    }

    return P.finish();
}
