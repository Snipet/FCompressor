// FCMP_PROBE layer=proc name=fixtures scope=global timeout=180
//
// proc.fixtures (P2, S8; 03 §3.5; 01 §9.1; C §5.7.3-5; K2 #10, #23, #25c): stored sessions load into a NON-FRESH
// instance (another Mode, every parameter elsewhere, the other UiState, prepared and processing) with the expected Mode,
// values, notice and editor state, and then sound exactly like the same session loaded into a fresh instance.
//
// Released fixtures: tests/fixtures/state/<stateVersion>-<key>-<desc>.bin, write-once blobs saved by a shipped build,
// each with its write-once sidecar <name>.expect (tab-separated: `modeId <key>`, `modeRev <n>`, `charExpanded 0|1`,
// `scTab sidechain|colour`, `param <id> <raw value %.9g>` per restored parameter). None exist before v1. The expected
// Mode and notice are derived from the saved identity and TODAY's registry (C §5.7.5: a key retired since loads its
// successor with modeMigrated; a Mode revised since gets modeRevised); a parameter the sidecar does not list (one
// appended later) expects its table default.
// Synthesised blobs (in-probe, hand-written XML in the layouts sessions really have; the evidence until v1):
//   1-<key>-odd                    every registered Mode: the v1 layout, 29 odd values (next-float nudged, step plains,
//                                  switch positions, listen/delta ON), <UI charExpanded="1" scTab="colour"/>
//   0-fet-76-dev                   P1's development layout: no stateVersion/modeId/modeRev, no <UI>; the `mode` PARAM
//                                  names fet-76
//   1-unknown-key                  modeId "vari-99" (unknown), modeRev 3, `mode` 100 (unassigned) -> clean, migrated
//   1-unknown-key-known-slot       modeId "gone-mode" (unknown), `mode` 2 -> clean (modeId wins), migrated
//   1-slot-unassigned              no modeId, `mode` 100 -> clean, migrated from "slot-100"
//   1-<retired>-retired[-slot]     every FCMP_RETIRED row (none yet): by key and by slot -> the successor, migrated
//   1-fet-76-rev0                  modeRev 0 (older than every revision) -> fet-76, modeRevised 0 -> current
//   1-bus-g-norev                  no modeRev (the first revision) -> no notice
//   2-clean-newer                  stateVersion 2, an unknown PARAM, an unknown child, an unknown root attribute ->
//                                  newerSession, known values exact; a re-save carries none of the unknowns
//   1-clean-sparse                 3 PARAMs -> every other parameter at its exact table default
//   1-hostile-values               nan, inf, -inf, 1e30, "abc", "", " 12.5 ", "7,5", "0x1p-1", "3.7" (Int), "2.4"
//                                  (`mode`, no modeId), out-of-range choices, a duplicate id -> sanitised or default
//   1-clean-badversion             stateVersion "abc" -> 1
// Rows per blob (spec), <group> = released.<name> or synth.<name> (a released fixture may share a synthesised name):
// fixtures.<group>.values (raw values bitwise: 0 wrong), .mode (1), .notice (0 wrong: serial + 1, the three flags,
// fromKey, toKey, savedRev/currentRev), .ui (1), .render (after prepareToPlay, 16 blocks of noise: output bitwise equal
// to a fresh instance that loaded the same blob: 0 differing samples), .nonfinite (0).
// Released fixtures add the golden row fixtures.released.<name>.render.rms_db (absrel; 03 §3.5 "render fingerprints").
// Other rows (spec):
//   fixtures.hostile.<name>.changed  a blob that is not a <PARAMS> tree (empty, nullptr, garbage, truncated, another
//                                    root, bare XML text, a size header that cuts the XML) changes nothing: raw values,
//                                    UiState and StateNotice (serial included) identical: 0
//   fixtures.synth.2-clean-newer.resave.unknown  0
//   fixtures.lookup.mismatches       fcmp::resolveSessionMode over a SYNTHETIC registry (clean 0, bus-g 1, fet-76 2 at
//                                    revision 3; retired old-comp 9 -> bus-g, older-comp 10 -> fet-76): retired keys and
//                                    slots, revisions older/equal/absent, unknown keys, unassigned slots, modeId against
//                                    a conflicting slot: 0
//   fixtures.migrations.size         stateMigrations() has kStateVersion - 1 entries
//   fixtures.candidates.mismatches   the candidates below, read back through the released-fixture path (sidecar and
//                                    registry rule) into non-fresh instances: 0
// Candidates (never adopted here; the lead adopts write-once fixtures from a SHIPPED build, SPRINTS §13 step 6):
// <build>/fixture-candidates/1-<key>-{odd,default}.{bin,expect} for every registered Mode, saved by THIS build's
// getStateInformation (<build> = the parent of --bless-to).
//
// Probe-own flags (after a lone "--"): --fixtures <dir>  read released fixtures from <dir> instead of
// tests/fixtures/state (e.g. a build's fixture-candidates, to review them; their render rows then have no golden).
#include "ProbeRegistry.h"

#include "EngineRig.h"
#include "Signals.h"

#include "plugin/Processor.h"
#include "plugin/State.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <crt_externs.h>                                         // _NSGetArgc / _NSGetArgv (macOS)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using fcdsp::HostParam;
    using fcdsp::Kind;
    using fcdsp::Map;
    using fcdsp::Pid;
    using funkgui::test::Probe;
    namespace fs = std::filesystem;
    namespace sig = fcmp::probe::sig;

    using Values = std::array<float, fcdsp::kNumParams>;         // Pid order

    constexpr double kFs = 48000.0;
    constexpr int kBlock = 256;
    constexpr int kRenderBlocks = 16;

    std::uint32_t bitsOf(float v)
    {
        std::uint32_t b = 0;
        std::memcpy(&b, &v, sizeof b);
        return b;
    }

    std::string fmt9(double v)
    {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.9g", v);
        return buf;
    }

    float frac(int salt)
    {
        const double f = 0.2360679774997897 + 0.6180339887498949 * static_cast<double>(salt);
        return static_cast<float>(0.07 + 0.86 * (f - std::floor(f)));
    }

    std::size_t pidIndex(std::string_view id)
    {
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            if (id == fcdsp::kHostParams[i].id)
                return i;
        return fcdsp::kNumParams;
    }

    const fcdsp::ModeEntry& entryOf(std::string_view key) { return fcmp::probe::modeEntry(key); }

    // The registered Mode after `key` (wrapping): the Mode every receiving instance runs before the load.
    const fcdsp::ModeEntry& otherThan(std::string_view key)
    {
        const std::span<const fcdsp::ModeSlot> slots = fcdsp::modeSlots();
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].key == key)
                return *slots[(i + 1) % slots.size()].entry;
        return *slots.front().entry;
    }

    std::optional<std::string> argValue(std::string_view flag, bool own)
    {
        const int argc = *_NSGetArgc();
        char** argv = *_NSGetArgv();
        bool afterDashes = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a = argv[i] != nullptr ? argv[i] : "";
            if (a == "--")
                afterDashes = true;
            else if (afterDashes == own && a == flag && i + 1 < argc && argv[i + 1] != nullptr)
                return std::string(argv[i + 1]);
        }
        return std::nullopt;
    }

    // ---- processors ---------------------------------------------------------------------------------------------------

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

    struct Rendered
    {
        std::vector<float> out;
        std::int64_t nonfinite = 0;
    };

    Rendered render(fcmp::Processor& proc, int blocks, std::uint64_t seed)
    {
        sig::Pcg32 rng(seed);
        juce::AudioBuffer<float> buf(2, kBlock);
        juce::MidiBuffer midi;
        Rendered r;
        r.out.reserve(static_cast<std::size_t>(blocks * kBlock * 2));
        for (int b = 0; b < blocks; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < kBlock; ++n)
                    buf.setSample(c, n, 0.4f * rng.bipolar());
            proc.processBlock(buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < kBlock; ++n)
                {
                    const float y = buf.getSample(c, n);
                    r.nonfinite += std::isfinite(y) ? 0 : 1;
                    r.out.push_back(y);
                }
        }
        return r;
    }

    // A running instance of `mode` with every parameter at a salted odd value (host writes) and the non-default UiState.
    std::unique_ptr<fcmp::Processor> nonFresh(const fcdsp::ModeEntry& mode, int salt)
    {
        auto proc = std::make_unique<fcmp::Processor>();
        proc->beginBatch();
        setPlain(*proc, Pid::mode, static_cast<float>(fcdsp::slotOf(mode)));
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
        {
            const HostParam& h = fcdsp::kHostParams[i];
            const float f = frac(salt + static_cast<int>(i));
            setPlain(*proc, h.pid, h.map == Map::index ? static_cast<float>(1 + (salt + static_cast<int>(i)) % 7)
                                                       : h.lo + (h.hi - h.lo) * f);
        }
        setPlain(*proc, Pid::extkey, 0.83f);
        setPlain(*proc, Pid::listen, 1.0f);
        setPlain(*proc, Pid::delta, 1.0f);
        setPlain(*proc, Pid::bypass, 0.2f);
        setPlain(*proc, Pid::quality, 0.0f);
        setPlain(*proc, Pid::labudget, 1.0f);
        setPlain(*proc, Pid::output, -7.3f);                     // ADR-88: a v1 session brings it back to 0 dB
        proc->endBatch();
        proc->uiState() = fcmp::UiState{ true, fcmp::ScTab::colour };
        proc->prepareToPlay(kFs, kBlock);
        render(*proc, 6, 0x1234u + static_cast<std::uint64_t>(salt));
        return proc;
    }

    void load(fcmp::Processor& proc, const void* data, std::size_t size)
    {
        proc.setStateInformation(data, static_cast<int>(size));
    }

    void load(fcmp::Processor& proc, const juce::MemoryBlock& blob) { load(proc, blob.getData(), blob.getSize()); }

    juce::MemoryBlock blobOf(const juce::XmlElement& xml)
    {
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary(xml, blob);
        return blob;
    }

    // ---- sessions and expectations ------------------------------------------------------------------------------------

    struct Expect
    {
        std::string modeKey;                                     // the Mode that must load
        bool newer = false, migrated = false, revised = false;
        std::string fromKey;                                     // when migrated
        int savedRev = 0, currentRev = 0;                        // when revised
        fcmp::UiState ui{};
        Values values{};                                         // every raw value after the load, bitwise
    };

    struct Param
    {
        std::string id, value;
    };

    struct Case
    {
        std::string name;
        std::vector<std::pair<std::string, std::string>> attrs; // root attributes, in order
        std::vector<Param> params;                               // PARAM children, in order (duplicates allowed)
        std::optional<fcmp::UiState> ui;                         // the <UI> child, when present
        bool unknowns = false;                                   // + <FUTURE/>, PARAM "futureparam", root "newAttr"
        Expect expect;
    };

    juce::MemoryBlock blobOf(const Case& c)
    {
        juce::XmlElement root("PARAMS");
        for (const auto& [k, v] : c.attrs)
            root.setAttribute(juce::Identifier(k.c_str()), juce::String(v));
        if (c.unknowns)
            root.setAttribute("newAttr", "x");
        for (const Param& p : c.params)
        {
            juce::XmlElement* e = root.createNewChildElement("PARAM");
            e->setAttribute("id", juce::String(p.id));
            e->setAttribute("value", juce::String(p.value));
        }
        if (c.unknowns)
        {
            juce::XmlElement* e = root.createNewChildElement("PARAM");
            e->setAttribute("id", "futureparam");
            e->setAttribute("value", "3");
            root.createNewChildElement("FUTURE")->setAttribute("a", "1");
        }
        if (c.ui.has_value())
        {
            juce::XmlElement* e = root.createNewChildElement("UI");
            e->setAttribute("charExpanded", c.ui->charExpanded ? "1" : "0");
            e->setAttribute("scTab", c.ui->scTab == fcmp::ScTab::colour ? "colour" : "sidechain");
        }
        return blobOf(root);
    }

    // Odd values for all 29 parameters of `en` as the texts a session stores (each text is an exact float; continuous
    // values are nudged to the next float, so most are not host-map fixed points), chosen under each parameter's active
    // spec in kResolveOrder. listen/delta ON; odd switch positions; quality HQ; budget 20 MS.
    std::vector<Param> oddParams(const fcdsp::ModeEntry& en, int salt)
    {
        fcdsp::RawParams raw = fcmp::probe::modeRaw(en);
        for (const Pid pid : fcdsp::kResolveOrder)
        {
            fcdsp::ParamView view;
            fcdsp::resolveView(*en.desc, raw, view);
            const std::size_t i = fcdsp::idx(pid);
            const HostParam& h = fcdsp::kHostParams[i];
            const fcdsp::ParamSpec* spec = view.spec[i];
            const int s = salt + static_cast<int>(i);
            const float f = frac(s);
            float v = 0.0f;
            if (h.map == Map::boolean)
                v = 0.3f + 0.4f * f;
            else if (spec != nullptr && (spec->kind == Kind::stepped || spec->kind == Kind::hybrid)
                     && !spec->steps.empty())
            {
                const std::size_t n = spec->steps.size();
                std::size_t k = static_cast<std::size_t>(s) % n;
                if (n > 1 && spec->steps[k].plain == raw.v[i])
                    k = (k + 1) % n;
                v = spec->steps[k].plain;
            }
            else if (h.map == Map::index)
                v = static_cast<float>(1 + s % (h.numSteps - 1));
            else
            {
                const bool live = spec != nullptr && spec->kind == Kind::continuous && spec->lo < spec->hi;
                const float lo = live ? spec->lo : h.lo, hi = live ? spec->hi : h.hi;
                v = std::nextafter(lo + (hi - lo) * f, hi);
            }
            raw.v[i] = v;
        }
        std::vector<Param> params;
        for (const Pid pid : fcdsp::kApvtsOrder)
        {
            const std::size_t i = fcdsp::idx(pid);
            float v = i < fcdsp::kNumModeParams ? raw.v[i] : 0.0f;
            if (pid == Pid::mode)
                v = static_cast<float>(fcdsp::slotOf(en));
            else if (pid == Pid::extkey)
                v = 0.61f;
            else if (pid == Pid::listen || pid == Pid::delta)
                v = 1.0f;
            else if (pid == Pid::bypass)
                v = 0.29f;
            else if (pid == Pid::quality || pid == Pid::labudget)
                v = 2.0f;
            params.push_back({ fcdsp::kHostParams[i].id, fmt9(static_cast<double>(v)) });
        }
        return params;
    }

    // The raw values a well-formed session restores: its values (the first of each known id), else table defaults;
    // listen/delta 0; `mode` = the slot of the Mode that loads.
    Values restoredValues(const std::vector<Param>& params, std::string_view modeKey)
    {
        Values v{};
        std::array<bool, fcdsp::kNumParams> seen{};
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            v[i] = fcdsp::kHostParams[i].def;
        for (const Param& p : params)
            if (const std::size_t i = pidIndex(p.id); i < fcdsp::kNumParams && !seen[i])
            {
                v[i] = static_cast<float>(std::strtod(p.value.c_str(), nullptr));
                seen[i] = true;
            }
        v[fcdsp::idx(Pid::listen)] = 0.0f;
        v[fcdsp::idx(Pid::delta)] = 0.0f;
        v[fcdsp::idx(Pid::mode)] = static_cast<float>(fcdsp::slotOf(entryOf(modeKey)));
        return v;
    }

    std::vector<std::pair<std::string, std::string>> v1Attrs(std::string_view key, int rev)
    {
        return { { "stateVersion", "1" }, { "modeId", std::string(key) }, { "modeRev", std::to_string(rev) },
                 { "product", "FCompressor" }, { "build", "0.1.0" } };
    }

    void setParam(std::vector<Param>& params, std::string_view id, std::string value)
    {
        for (Param& p : params)
            if (p.id == id)
            {
                p.value = std::move(value);
                return;
            }
        params.push_back({ std::string(id), std::move(value) });
    }

    int revisionOf(std::string_view key) { return entryOf(key).desc->revision; }

    // ---- checking one session -----------------------------------------------------------------------------------------

    struct Result
    {
        std::int64_t values = 0, notice = 0, render = 0, nonfinite = 0;
        bool mode = false, ui = false;
        double rmsDb = 0.0;
        std::int64_t wrong() const { return values + notice + render + nonfinite + (mode ? 0 : 1) + (ui ? 0 : 1); }
    };

    Result check(const std::string& name, const juce::MemoryBlock& blob, const Expect& ex, int salt)
    {
        Result r;
        auto x = nonFresh(otherThan(ex.modeKey), salt);
        const std::uint32_t serial = x->stateNotice().serial;
        load(*x, blob);

        const Values got = rawValues(*x);
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            if (bitsOf(got[i]) != bitsOf(ex.values[i]))
            {
                if (r.values < 3)
                    std::printf("NOTE     fixtures.%s %s: want %.9g got %.9g\n", name.c_str(), fcdsp::kHostParams[i].id,
                                static_cast<double>(ex.values[i]), static_cast<double>(got[i]));
                ++r.values;
            }
        const std::string_view played = fcdsp::resolveSlot(x->currentRaw().modeSlot).key;
        r.mode = played == ex.modeKey;

        const fcmp::StateNotice n = x->stateNotice();
        const std::string_view toKey = (ex.migrated || ex.revised) ? std::string_view(ex.modeKey) : std::string_view();
        r.notice += n.serial == serial + 1u ? 0 : 1;
        r.notice += n.newerSession == ex.newer ? 0 : 1;
        r.notice += n.modeMigrated == ex.migrated ? 0 : 1;
        r.notice += n.modeRevised == ex.revised ? 0 : 1;
        r.notice += std::string_view(n.fromKey) == (ex.migrated ? std::string_view(ex.fromKey) : std::string_view()) ? 0 : 1;
        r.notice += std::string_view(n.toKey) == toKey ? 0 : 1;
        r.notice += n.savedRev == (ex.revised ? ex.savedRev : 0) && n.currentRev == (ex.revised ? ex.currentRev : 0) ? 0 : 1;
        if (r.notice != 0)
            std::printf("NOTE     fixtures.%s notice: serial +%u newer %d migrated %d '%s' -> '%s' revised %d %u -> %u\n",
                        name.c_str(), n.serial - serial, n.newerSession ? 1 : 0, n.modeMigrated ? 1 : 0, n.fromKey,
                        n.toKey, n.modeRevised ? 1 : 0, static_cast<unsigned>(n.savedRev),
                        static_cast<unsigned>(n.currentRev));
        r.ui = x->uiState().charExpanded == ex.ui.charExpanded && x->uiState().scTab == ex.ui.scTab;

        // The same session in a fresh instance (the host's usual order: setStateInformation, then prepareToPlay) and in
        // the running one re-prepared: identical output, so nothing of the previous session survives the load.
        auto y = std::make_unique<fcmp::Processor>();
        load(*y, blob);
        x->prepareToPlay(kFs, kBlock);
        y->prepareToPlay(kFs, kBlock);
        const Rendered rx = render(*x, kRenderBlocks, 0x5eedu);
        const Rendered ry = render(*y, kRenderBlocks, 0x5eedu);
        double energy = 0.0;
        for (std::size_t i = 0; i < ry.out.size(); ++i)
        {
            r.render += bitsOf(rx.out[i]) == bitsOf(ry.out[i]) ? 0 : 1;
            energy += static_cast<double>(ry.out[i]) * static_cast<double>(ry.out[i]);
        }
        r.nonfinite = rx.nonfinite + ry.nonfinite;
        const double ms = energy / static_cast<double>(std::max<std::size_t>(ry.out.size(), 1));
        r.rmsDb = 10.0 * std::log10(std::max(ms, 1e-30));
        return r;
    }

    void rows(Probe& P, const std::string& name, const Result& r)
    {
        const std::string k = "fixtures." + name;
        std::printf("NOTE     %s: rendered at %.2f dB RMS\n", k.c_str(), r.rmsDb);
        P.eq(k + ".values", r.values, 0);
        P.eq(k + ".mode", r.mode ? 1 : 0, 1);
        P.eq(k + ".notice", r.notice, 0);
        P.eq(k + ".ui", r.ui ? 1 : 0, 1);
        P.eq(k + ".render", r.render, 0);
        P.eq(k + ".nonfinite", r.nonfinite, 0);
    }

    // ---- released fixtures: the sidecar and the registry rule -----------------------------------------------------------

    struct Sidecar
    {
        std::string modeId;
        std::optional<int> modeRev;
        fcmp::UiState ui{};
        std::array<std::optional<float>, fcdsp::kNumParams> values{};
    };

    bool readSidecar(const fs::path& file, Sidecar& s, std::string& error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot read " + file.string();
            return false;
        }
        std::string line;
        int n = 0;
        while (std::getline(in, line))
        {
            ++n;
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream f(line);
            std::string field, a, b;
            std::getline(f, field, '\t');
            std::getline(f, a, '\t');
            std::getline(f, b, '\t');
            char* end = nullptr;
            if (field == "modeId")
                s.modeId = a;
            else if (field == "modeRev")
                s.modeRev = static_cast<int>(std::strtol(a.c_str(), &end, 10));
            else if (field == "charExpanded")
                s.ui.charExpanded = a == "1";
            else if (field == "scTab")
                s.ui.scTab = a == "colour" ? fcmp::ScTab::colour : fcmp::ScTab::sidechain;
            else if (field == "param" && pidIndex(a) < fcdsp::kNumParams)
            {
                const double v = std::strtod(b.c_str(), &end);
                if (end == b.c_str() || *end != '\0')
                {
                    error = file.string() + ":" + std::to_string(n) + ": bad value '" + b + "'";
                    return false;
                }
                s.values[pidIndex(a)] = static_cast<float>(v);
            }
            else if (field != "param")
            {
                error = file.string() + ":" + std::to_string(n) + ": unknown field '" + field + "'";
                return false;
            }
        }
        if (s.modeId.empty())
        {
            error = file.string() + ": no modeId";
            return false;
        }
        return true;
    }

    // C §5.7.5 restated independently of State.cpp: registered -> itself; retired -> its successor, migrated; unknown ->
    // clean, migrated; not migrated and an older revision -> revised.
    Expect expectFromSidecar(const Sidecar& s)
    {
        Expect ex;
        if (fcdsp::byKey(s.modeId) != nullptr)
            ex.modeKey = s.modeId;
        else
        {
            ex.migrated = true;
            ex.fromKey = s.modeId;
            ex.modeKey = "clean";
            for (const fcdsp::Retired& r : fcdsp::retired())
                if (r.key == s.modeId)
                    ex.modeKey = std::string(r.successor);
        }
        const int current = revisionOf(ex.modeKey);
        if (!ex.migrated && s.modeRev.value_or(1) < current)
        {
            ex.revised = true;
            ex.savedRev = s.modeRev.value_or(1);
            ex.currentRev = current;
        }
        ex.ui = s.ui;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            ex.values[i] = s.values[i].value_or(fcdsp::kHostParams[i].def);
        ex.values[fcdsp::idx(Pid::listen)] = 0.0f;
        ex.values[fcdsp::idx(Pid::delta)] = 0.0f;
        ex.values[fcdsp::idx(Pid::mode)] = static_cast<float>(fcdsp::slotOf(entryOf(ex.modeKey)));
        return ex;
    }

    void writeSidecar(const fs::path& file, const std::string& modeId, int modeRev, const fcmp::UiState& ui,
                      const Values& values)
    {
        std::ofstream out(file, std::ios::trunc);
        out << "# proc.fixtures expectations (write-once with the .bin): the restored raw value of every parameter\n";
        out << "modeId\t" << modeId << "\nmodeRev\t" << modeRev << "\ncharExpanded\t" << (ui.charExpanded ? 1 : 0)
            << "\nscTab\t" << (ui.scTab == fcmp::ScTab::colour ? "colour" : "sidechain") << '\n';
        for (const Pid pid : fcdsp::kApvtsOrder)
            out << "param\t" << fcdsp::kHostParams[fcdsp::idx(pid)].id << '\t'
                << fmt9(static_cast<double>(values[fcdsp::idx(pid)])) << '\n';
    }

    // Every <dir>/*.bin with its sidecar, in name order. Returns the number checked; rowsPerFixture: one row group per
    // fixture (released), else the total is added to `wrong` (candidates).
    int checkDirectory(Probe& P, const fs::path& dir, bool rowsPerFixture, bool goldenRender, std::int64_t& wrong)
    {
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
            return 0;
        std::vector<fs::path> bins;
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".bin")
                bins.push_back(e.path());
        std::sort(bins.begin(), bins.end());
        int salt = 300;
        for (const fs::path& bin : bins)
        {
            const std::string name = "released." + bin.stem().string();   // never a synthesised blob's row group
            Sidecar s;
            std::string error;
            if (!readSidecar(fs::path(bin).replace_extension(".expect"), s, error))
            {
                P.harnessError("proc.fixtures: " + error);
                continue;
            }
            std::ifstream in(bin, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            juce::MemoryBlock blob(bytes.data(), bytes.size());
            const Expect ex = expectFromSidecar(s);
            const Result r = check(name, blob, ex, salt += 7);
            if (rowsPerFixture)
            {
                rows(P, name, r);
                if (goldenRender)
                    P.num("fixtures." + name + ".render.rms_db", r.rmsDb, funkgui::test::Tol::absrel(1e-3, 1e-4));
            }
            else
                wrong += r.wrong();
        }
        return static_cast<int>(bins.size());
    }

    // ---- the synthetic registry for resolveSessionMode ----------------------------------------------------------------

    struct FakeRegistry
    {
        std::array<fcdsp::ModeDescriptor, 3> desc;
        std::array<fcdsp::ModeEntry, 3> entry;
        std::array<fcdsp::ModeSlot, 3> slots;
        std::array<fcdsp::Retired, 2> retired;
    };
    const FakeRegistry* gFake = nullptr;

    const fcdsp::ModeSlot* fakeResolveKey(std::string_view key) noexcept
    {
        for (const fcdsp::ModeSlot& m : gFake->slots)
            if (m.key == key)
                return &m;
        for (const fcdsp::Retired& r : gFake->retired)
            if (r.key == key)
                return fakeResolveKey(r.successor);
        return nullptr;
    }

    const fcdsp::ModeSlot& fakeResolveSlot(int slot) noexcept
    {
        for (const fcdsp::ModeSlot& m : gFake->slots)
            if (m.slot == slot)
                return m;
        for (const fcdsp::Retired& r : gFake->retired)
            if (r.slot == slot)
                return *fakeResolveKey(r.successor);
        return gFake->slots[0];                                  // clean
    }

    std::span<const fcdsp::Retired> fakeRetired() noexcept { return { gFake->retired.data(), gFake->retired.size() }; }

    std::int64_t checkLookup()
    {
        FakeRegistry reg{};
        const char* keys[3] = { "clean", "bus-g", "fet-76" };
        for (std::size_t i = 0; i < 3; ++i)
        {
            reg.desc[i] = *entryOf(keys[i]).desc;
            reg.desc[i].revision = i == 2 ? 3 : 1;
            reg.entry[i] = entryOf(keys[i]);
            reg.entry[i].desc = &reg.desc[i];
            reg.slots[i] = fcdsp::ModeSlot{ static_cast<std::uint8_t>(i), keys[i], &reg.entry[i] };
        }
        reg.retired[0] = fcdsp::Retired{ 9, "old-comp", "bus-g" };
        reg.retired[1] = fcdsp::Retired{ 10, "older-comp", "fet-76" };
        gFake = &reg;
        const fcmp::ModeLookup lookup{ &fakeResolveKey, &fakeResolveSlot, &fakeRetired };

        struct T
        {
            const char* what;
            fcmp::SavedMode in;
            const char* key;
            bool migrated;
            const char* from;
            bool revised;
            int savedRev, currentRev;
        };
        using S = fcmp::SavedMode;
        const T cases[] = {
            { "retired key", S{ "old-comp", 9, 1 }, "bus-g", true, "old-comp", false, 0, 0 },
            { "retired key, other slot", S{ "older-comp", 0, 1 }, "fet-76", true, "older-comp", false, 0, 0 },
            { "older revision", S{ "fet-76", 2, 2 }, "fet-76", false, "", true, 2, 3 },
            { "current revision", S{ "fet-76", 2, 3 }, "fet-76", false, "", false, 0, 0 },
            { "newer revision", S{ "fet-76", 2, 4 }, "fet-76", false, "", false, 0, 0 },
            { "absent revision = 1", S{ "fet-76", 2, std::nullopt }, "fet-76", false, "", true, 1, 3 },
            { "retired slot", S{ std::nullopt, 10, 1 }, "fet-76", true, "older-comp", false, 0, 0 },
            { "unassigned slot", S{ std::nullopt, 50, std::nullopt }, "clean", true, "slot-50", false, 0, 0 },
            { "unknown key", S{ "nope", 1, 1 }, "clean", true, "nope", false, 0, 0 },
            { "modeId wins over the slot", S{ "bus-g", 2, 1 }, "bus-g", false, "", false, 0, 0 },
            { "nothing saved", S{}, "clean", false, "", false, 0, 0 },
            { "slot only, older revision", S{ std::nullopt, 2, 1 }, "fet-76", false, "", true, 1, 3 },
        };
        std::int64_t bad = 0;
        for (const T& t : cases)
        {
            const fcmp::SessionMode m = fcmp::resolveSessionMode(t.in, lookup);
            const bool ok = m.mode != nullptr && m.mode->key == t.key && m.migrated == t.migrated
                         && m.fromKey == (t.migrated ? t.from : "") && m.revised == t.revised
                         && m.savedRev == t.savedRev && m.currentRev == t.currentRev;
            if (!ok)
            {
                std::printf("NOTE     fixtures.lookup '%s': got %.*s migrated %d '%s' revised %d %u -> %u\n", t.what,
                            m.mode != nullptr ? static_cast<int>(m.mode->key.size()) : 0,
                            m.mode != nullptr ? m.mode->key.data() : "", m.migrated ? 1 : 0, m.fromKey.c_str(),
                            m.revised ? 1 : 0, static_cast<unsigned>(m.savedRev), static_cast<unsigned>(m.currentRev));
                ++bad;
            }
        }
        gFake = nullptr;
        return bad;
    }

    // ---- hostile blobs ------------------------------------------------------------------------------------------------

    std::int64_t checkIgnored(const void* data, std::size_t size, int salt)
    {
        auto x = nonFresh(entryOf("bus-g"), salt);
        const Values before = rawValues(*x);
        const fcmp::UiState ui = x->uiState();
        const fcmp::StateNotice n0 = x->stateNotice();
        x->setStateInformation(data, static_cast<int>(size));
        const Values after = rawValues(*x);
        std::int64_t changed = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            changed += bitsOf(after[i]) == bitsOf(before[i]) ? 0 : 1;
        changed += x->uiState().charExpanded == ui.charExpanded && x->uiState().scTab == ui.scTab ? 0 : 1;
        const fcmp::StateNotice n1 = x->stateNotice();
        const bool same = n0.serial == n1.serial && n0.newerSession == n1.newerSession
                       && n0.modeMigrated == n1.modeMigrated && n0.modeRevised == n1.modeRevised
                       && n0.savedRev == n1.savedRev && n0.currentRev == n1.currentRev
                       && std::string_view(n0.fromKey) == std::string_view(n1.fromKey)
                       && std::string_view(n0.toKey) == std::string_view(n1.toKey);
        changed += same ? 0 : 1;
        return changed;
    }
} // namespace

FCMP_PROBE(proc, fixtures)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    std::int64_t unused = 0;

    // ---- released fixtures --------------------------------------------------------------------------------------------
    {
        fs::path dir;
        if (const std::optional<std::string> own = argValue("--fixtures", true))
            dir = *own;
        else if (const std::optional<std::string> golden = argValue("--golden-root", false))
            dir = fs::path(*golden).parent_path() / "fixtures" / "state";
        const int n = dir.empty() ? 0 : checkDirectory(P, dir, true, true, unused);
        std::printf("NOTE     fixtures: %d released fixtures in %s%s\n", n, dir.c_str(),
                    n == 0 ? " (none before v1: the synthesised blobs below are the evidence)" : "");
    }

    // ---- synthesised blobs --------------------------------------------------------------------------------------------
    std::vector<Case> cases;
    int salt = 1;
    for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
    {
        Case c;
        c.name = "1-" + std::string(m.key) + "-odd";
        c.attrs = v1Attrs(m.key, m.entry->desc->revision);
        c.params = oddParams(*m.entry, salt += 11);
        c.ui = fcmp::UiState{ true, fcmp::ScTab::colour };
        c.expect.modeKey = std::string(m.key);
        c.expect.ui = *c.ui;
        c.expect.values = restoredValues(c.params, m.key);
        cases.push_back(std::move(c));
    }
    {
        Case c;                                                  // P1's development layout
        c.name = "0-fet-76-dev";
        c.params = oddParams(entryOf("fet-76"), 500);
        c.expect.modeKey = "fet-76";
        c.expect.values = restoredValues(c.params, "fet-76");
        cases.push_back(std::move(c));
    }
    const auto unknownCase = [&](std::string name, std::optional<std::string> modeId, int slot, std::string from) {
        Case c;
        c.name = std::move(name);
        c.attrs = { { "stateVersion", "1" } };
        if (modeId.has_value())
            c.attrs.push_back({ "modeId", *modeId });
        c.attrs.push_back({ "modeRev", "3" });
        c.params = oddParams(entryOf("clean"), 600 + slot);
        setParam(c.params, "mode", std::to_string(slot));
        c.ui = fcmp::UiState{ false, fcmp::ScTab::colour };
        c.expect.modeKey = "clean";
        c.expect.migrated = true;
        c.expect.fromKey = std::move(from);
        c.expect.ui = *c.ui;
        c.expect.values = restoredValues(c.params, "clean");
        return c;
    };
    cases.push_back(unknownCase("1-unknown-key", "vari-99", 100, "vari-99"));
    cases.push_back(unknownCase("1-unknown-key-known-slot", "gone-mode", 2, "gone-mode"));
    cases.push_back(unknownCase("1-slot-unassigned", std::nullopt, 100, "slot-100"));
    for (const fcdsp::Retired& r : fcdsp::retired())
        for (const bool bySlot : { false, true })
        {
            Case c = unknownCase("1-" + std::string(r.key) + (bySlot ? "-retired-slot" : "-retired"),
                                 bySlot ? std::nullopt : std::optional<std::string>(r.key), r.slot, std::string(r.key));
            c.expect.modeKey = std::string(r.successor);
            c.expect.values = restoredValues(c.params, r.successor);
            cases.push_back(std::move(c));
        }
    std::printf("NOTE     fixtures: %zu retired Modes in Modes.def (the retired branch is also proven by "
                "fixtures.lookup on a synthetic registry)\n", fcdsp::retired().size());
    {
        Case c;                                                  // an older revision than any Mode has
        c.name = "1-fet-76-rev0";
        c.attrs = v1Attrs("fet-76", 0);
        c.params = oddParams(entryOf("fet-76"), 700);
        c.expect.modeKey = "fet-76";
        c.expect.revised = true;
        c.expect.savedRev = 0;
        c.expect.currentRev = revisionOf("fet-76");
        c.expect.values = restoredValues(c.params, "fet-76");
        cases.push_back(std::move(c));
    }
    {
        Case c;                                                  // no modeRev: the first revision
        c.name = "1-bus-g-norev";
        c.attrs = { { "stateVersion", "1" }, { "modeId", "bus-g" } };
        c.params = oddParams(entryOf("bus-g"), 710);
        c.expect.modeKey = "bus-g";
        c.expect.revised = revisionOf("bus-g") > 1;
        c.expect.savedRev = c.expect.revised ? 1 : 0;
        c.expect.currentRev = c.expect.revised ? revisionOf("bus-g") : 0;
        c.expect.values = restoredValues(c.params, "bus-g");
        cases.push_back(std::move(c));
    }
    {
        Case c;                                                  // a session from a newer build
        c.name = "2-clean-newer";
        c.attrs = v1Attrs("clean", 1);
        c.attrs[0].second = "2";
        c.params = oddParams(entryOf("clean"), 720);
        c.unknowns = true;
        c.ui = fcmp::UiState{ true, fcmp::ScTab::sidechain };
        c.expect.modeKey = "clean";
        c.expect.newer = true;
        c.expect.ui = *c.ui;
        c.expect.values = restoredValues(c.params, "clean");
        cases.push_back(std::move(c));
    }
    {
        Case c;                                                  // most parameters absent
        c.name = "1-clean-sparse";
        c.attrs = v1Attrs("clean", 1);
        c.params = { { "thr", "-31.25" }, { "ratio", fmt9(0.6180339887) }, { "mode", "0" } };
        c.expect.modeKey = "clean";
        c.expect.values = restoredValues(c.params, "clean");
        cases.push_back(std::move(c));
    }
    {
        Case c;                                                  // every kind of bad value
        c.name = "1-hostile-values";
        c.attrs = { { "stateVersion", "1" }, { "modeRev", "abc" } };
        c.params = { { "thr", "nan" },        { "thr", "-30" },       { "ratio", "inf" },     { "knee", "-inf" },
                     { "range", "1e30" },     { "atk", "abc" },       { "rel", "" },          { "hold", " 12.5 " },
                     { "look", "-5" },        { "makeup", "7,5" },    { "mix", "0x1p-1" },    { "det", "3.7" },
                     { "mode", "2.4" },       { "quality", "9" },     { "labudget", "-1" },   { "bypass", "0.75" },
                     { "extkey", "-3" },      { "sce", "1.5e0" },     { "listen", "1" },      { "voice", "7.5" } };
        c.expect.modeKey = "fet-76";                             // slot 2.4 -> 2 (no modeId)
        Values& v = c.expect.values;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            v[i] = fcdsp::kHostParams[i].def;
        const auto set = [&v](Pid p, float x) { v[fcdsp::idx(p)] = x; };
        set(Pid::thr, -30.0f);                                   // the first number of the id
        set(Pid::range, 60.0f);                                  // clamped to the host range
        set(Pid::hold, 12.5f);
        set(Pid::look, 0.0f);
        set(Pid::det, 4.0f);                                     // Int: rounded
        set(Pid::mode, static_cast<float>(fcdsp::slotOf(entryOf("fet-76"))));
        set(Pid::quality, 2.0f);                                 // clamped
        set(Pid::labudget, 0.0f);
        set(Pid::bypass, 0.75f);                                 // a switch keeps its position
        set(Pid::extkey, 0.0f);                                  // ... clamped to [0, 1]
        set(Pid::sce, 1.5f);
        set(Pid::voice, 7.0f);                                   // 7.5: clamped to the range end
        cases.push_back(std::move(c));
    }
    {
        Case c;
        c.name = "1-clean-badversion";
        c.attrs = v1Attrs("clean", 1);
        c.attrs[0].second = "abc";
        c.params = oddParams(entryOf("clean"), 730);
        c.expect.modeKey = "clean";
        c.expect.values = restoredValues(c.params, "clean");
        cases.push_back(std::move(c));
    }

    for (const Case& c : cases)
    {
        const juce::MemoryBlock blob = blobOf(c);
        rows(P, "synth." + c.name, check("synth." + c.name, blob, c.expect, salt += 13));
        if (c.unknowns)
        {
            auto x = nonFresh(entryOf("bus-g"), 800);
            load(*x, blob);
            juce::MemoryBlock resaved;
            x->getStateInformation(resaved);
            const std::unique_ptr<juce::XmlElement> xml =
                juce::AudioProcessor::getXmlFromBinary(resaved.getData(), static_cast<int>(resaved.getSize()));
            std::int64_t unknown = xml == nullptr ? 1 : 0;
            if (xml != nullptr)
            {
                unknown += xml->hasAttribute("newAttr") ? 1 : 0;
                unknown += xml->getChildByName("FUTURE") != nullptr ? 1 : 0;
                unknown += xml->getChildByAttribute("id", "futureparam") != nullptr ? 1 : 0;
                unknown += xml->getStringAttribute("stateVersion") == "1" ? 0 : 1;
            }
            P.eq("fixtures.synth." + c.name + ".resave.unknown", unknown, 0);
        }
    }

    // ---- not a <PARAMS> tree: nothing changes -------------------------------------------------------------------------
    {
        const Case& good = cases.front();
        const juce::MemoryBlock blob = blobOf(good);
        juce::XmlElement other("HRVB");
        other.createNewChildElement("PARAM")->setAttribute("id", "thr");
        const juce::MemoryBlock wrongRoot = blobOf(other);
        const juce::String text = "<PARAMS stateVersion=\"1\"><PARAM id=\"thr\" value=\"-3\"/></PARAMS>";
        juce::MemoryBlock lying = blob;
        if (lying.getSize() >= 8)                                // the size field cuts the XML in half
        {
            const std::uint32_t half = static_cast<std::uint32_t>(lying.getSize() - 8) / 2u;
            std::memcpy(static_cast<char*>(lying.getData()) + 4, &half, sizeof half);
        }
        std::vector<char> garbage(1024);
        sig::Pcg32 rng(99u);
        for (char& ch : garbage)
            ch = static_cast<char>(rng.next() & 0xffu);
        const std::uint8_t one = 0;
        struct H
        {
            const char* name;
            const void* data;
            std::size_t size;
        };
        const H hostile[] = {
            { "empty", &one, 0 },
            { "nullptr", nullptr, 64 },
            { "garbage", garbage.data(), garbage.size() },
            { "truncated", blob.getData(), blob.getSize() / 2 },
            { "wrong-root", wrongRoot.getData(), wrongRoot.getSize() },
            { "xml-text", text.toRawUTF8(), text.getNumBytesAsUTF8() },
            { "lying-size", lying.getData(), lying.getSize() },
        };
        int s = 900;
        for (const H& h : hostile)
            P.eq(std::string("fixtures.hostile.") + h.name + ".changed", checkIgnored(h.data, h.size, s += 3), 0);
    }

    // ---- the Mode rule on a synthetic registry; the migrations table ----------------------------------------------------
    P.eq("fixtures.lookup.mismatches", checkLookup(), 0);
    P.eq("fixtures.migrations.size", static_cast<std::int64_t>(fcmp::stateMigrations().size()),
         static_cast<std::int64_t>(fcdsp::kStateVersion) - 1);

    // ---- candidates: saved by this build, read back through the released-fixture path ---------------------------------
    if (const std::optional<std::string> blessTo = argValue("--bless-to", false))
    {
        const fs::path dir = fs::path(*blessTo).parent_path() / "fixture-candidates";
        std::error_code ec;
        fs::create_directories(dir, ec);
        int written = 0;
        for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
            for (const bool odd : { true, false })
            {
                auto y = std::make_unique<fcmp::Processor>();
                fcmp::UiState ui{};
                if (odd)
                {
                    const auto it = std::find_if(cases.begin(), cases.end(), [&](const Case& c) {
                        return c.name == "1-" + std::string(m.key) + "-odd";
                    });
                    load(*y, blobOf(*it));
                    ui = y->uiState();
                }
                else
                    setPlain(*y, Pid::mode, static_cast<float>(m.slot));
                const Values values = rawValues(*y);             // what a reload restores (listen/delta 0)
                setPlain(*y, Pid::listen, 1.0f);                 // saved ON: the fixture proves the reset too
                setPlain(*y, Pid::delta, 1.0f);
                juce::MemoryBlock blob;
                y->getStateInformation(blob);
                const std::string name = "1-" + std::string(m.key) + (odd ? "-odd" : "-default");
                const fs::path bin = dir / (name + ".bin"), expect = dir / (name + ".expect");
                const fs::path tmpBin = dir / (name + ".bin.tmp"), tmpExpect = dir / (name + ".expect.tmp");
                {
                    std::ofstream out(tmpBin, std::ios::binary | std::ios::trunc);
                    out.write(static_cast<const char*>(blob.getData()), static_cast<std::streamsize>(blob.getSize()));
                }
                writeSidecar(tmpExpect, std::string(m.key), m.entry->desc->revision, ui, values);
                fs::rename(tmpBin, bin, ec);
                if (!ec)
                    fs::rename(tmpExpect, expect, ec);
                if (ec)
                    P.harnessError("proc.fixtures: cannot write " + bin.string() + ": " + ec.message());
                else
                    ++written;
            }
        std::int64_t wrong = 0;
        const int read = checkDirectory(P, dir, false, false, wrong);
        P.eq("fixtures.candidates.mismatches", wrong, 0);
        std::printf("NOTE     fixtures: %d candidates written to %s, %d read back\n", written, dir.c_str(), read);
    }
    return P.finish();
}
