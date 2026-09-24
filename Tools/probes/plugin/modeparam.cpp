// FCMP_PROBE layer=proc name=modeparam scope=global timeout=60
//
// proc.modeparam (P2, S8; 03 §3.5; 01 §3.1 `mode`, §8.2; C §5.7.4; E §4.3): a host automates `mode` by its normalised
// value, so norm = slot/127 must keep naming the same Mode in every later build (the append-only 0..127 slot table).
//
// Rows (spec):
//   modeparam.fixture.mismatches   per row of tests/fixtures/modeparam.tsv (stateVersion, slot, norm, key; lead-owned,
//                                  write-once rows from each release; none before v1, the count is a NOTE; a malformed
//                                  row, a slot outside 0..127 or a stateVersion above this build's is a harness error):
//                                  norm == slot/127 (within 1e-6); a host setting `mode` to norm
//                                  (setValueNotifyingHost) plays `key`, or `key`'s successor if it was retired since,
//                                  and never anything for an unknown key: 0
//   modeparam.slots.mismatches     all 128 slots through the same host path: a registered slot plays its own Mode, a
//                                  retired one its successor, an unassigned one clean (01 §8.2): 0
//   modeparam.norm.mismatches      the `mode` parameter at slot s reports the normalised value s/127 (the value the rows
//                                  record, within 1e-7), and the host's s/127 reads back as raw slot s exactly: 0
//   modeparam.state.mismatches     a session whose only Mode information is the `mode` PARAM (P1's development format:
//                                  no modeId, no modeRev) loads, into a non-fresh instance, the Mode the host path
//                                  plays for that slot, for every slot: 0
// Candidates: <build>/fixture-candidates/modeparam.tsv (the build is --bless-to's parent) holds this build's rows, one
// per registered Mode, for the lead to append at a release. tests/fixtures is never written here.
//
// Probe-own flags (after a lone "--"): --fixtures <dir>  read <dir>/modeparam.tsv instead of tests/fixtures/.
#include "ProbeRegistry.h"

#include "plugin/Processor.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Pid.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <crt_externs.h>                                         // _NSGetArgc / _NSGetArgv (macOS)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using fcdsp::Pid;
    using funkgui::test::Probe;
    namespace fs = std::filesystem;

    constexpr int kSlots = fcdsp::kModeCapacity;                 // 128: norm = slot / 127

    // The value of a harness flag (before "--") or of a probe-own flag (after it).
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

    // The key a slot plays: registered -> its own, retired -> the successor, unassigned -> clean (01 §8.2).
    std::string_view expectedKey(int slot)
    {
        for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
            if (m.slot == slot)
                return m.key;
        for (const fcdsp::Retired& r : fcdsp::retired())
            if (r.slot == slot)
                return r.successor;
        return "clean";
    }

    // What a host setting `mode` to `norm` makes the processor play.
    std::string_view playedKey(fcmp::Processor& proc, float norm)
    {
        proc.parameter(Pid::mode).setValueNotifyingHost(norm);
        return fcdsp::resolveSlot(proc.currentRaw().modeSlot).key;
    }

    float slotNorm(int slot) { return static_cast<float>(slot) / static_cast<float>(kSlots - 1); }

    struct Row
    {
        int stateVersion = 0, slot = 0;
        double norm = 0.0;
        std::string key;
        int line = 0;
    };

    // tests/fixtures/modeparam.tsv: '#' comments and blank lines, else "stateVersion<TAB>slot<TAB>norm<TAB>key".
    bool readRows(const fs::path& file, std::vector<Row>& rows, std::string& error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot read " + file.string();
            return false;
        }
        std::string text;
        int line = 0;
        while (std::getline(in, text))
        {
            ++line;
            if (text.empty() || text[0] == '#')
                continue;
            std::istringstream fields(text);
            Row r;
            r.line = line;
            std::string sv, sl, sn;
            if (!std::getline(fields, sv, '\t') || !std::getline(fields, sl, '\t') || !std::getline(fields, sn, '\t')
                || !std::getline(fields, r.key, '\t'))
            {
                error = file.string() + ":" + std::to_string(line) + ": expected 4 tab-separated fields";
                return false;
            }
            char* end = nullptr;
            r.stateVersion = static_cast<int>(std::strtol(sv.c_str(), &end, 10));
            const bool okV = end != sv.c_str() && *end == '\0';
            r.slot = static_cast<int>(std::strtol(sl.c_str(), &end, 10));
            const bool okS = end != sl.c_str() && *end == '\0';
            r.norm = std::strtod(sn.c_str(), &end);
            const bool okN = end != sn.c_str() && *end == '\0' && std::isfinite(r.norm);
            if (!okV || !okS || !okN || r.key.empty() || r.slot < 0 || r.slot >= kSlots || r.stateVersion < 1
                || r.stateVersion > static_cast<int>(fcdsp::kStateVersion))
            {
                error = file.string() + ":" + std::to_string(line) + ": malformed row '" + text + "'";
                return false;
            }
            rows.push_back(std::move(r));
        }
        return true;
    }

    // Load a <PARAMS> session that holds only `mode` (and one other value, so it is not empty).
    void loadSlotOnly(fcmp::Processor& proc, int slot)
    {
        juce::XmlElement root("PARAMS");
        juce::XmlElement* m = root.createNewChildElement("PARAM");
        m->setAttribute("id", "mode");
        m->setAttribute("value", slot);
        juce::XmlElement* t = root.createNewChildElement("PARAM");
        t->setAttribute("id", "thr");
        t->setAttribute("value", -20.5);
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary(root, blob);
        proc.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    }
} // namespace

FCMP_PROBE(proc, modeparam)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;              // the processor's SetupWatcher is a juce::Timer
    auto proc = std::make_unique<fcmp::Processor>();

    // ---- released rows ------------------------------------------------------------------------------------------------
    fs::path fixtures;
    if (const std::optional<std::string> dir = argValue("--fixtures", true))
        fixtures = *dir;
    else if (const std::optional<std::string> golden = argValue("--golden-root", false))
        fixtures = fs::path(*golden).parent_path() / "fixtures";
    std::vector<Row> rows;
    if (fixtures.empty())
        P.harnessError("proc.modeparam: no --golden-root, so tests/fixtures/modeparam.tsv cannot be found");
    else if (std::string error; !readRows(fixtures / "modeparam.tsv", rows, error))
        P.harnessError("proc.modeparam: " + error);
    std::int64_t rowBad = 0;
    for (const Row& r : rows)
    {
        const fcdsp::ModeSlot* want = fcdsp::resolveKey(r.key);        // retired since -> its successor
        const std::string_view got = playedKey(*proc, static_cast<float>(r.norm));
        const bool ok = std::abs(r.norm - static_cast<double>(slotNorm(r.slot))) <= 1e-6 && want != nullptr
                     && got == want->key;
        if (!ok)
        {
            std::printf("NOTE     modeparam.fixture line %d: v%d slot %d norm %.9g key %s plays %.*s\n", r.line,
                        r.stateVersion, r.slot, r.norm, r.key.c_str(), static_cast<int>(got.size()), got.data());
            ++rowBad;
        }
    }
    P.eq("modeparam.fixture.mismatches", rowBad, 0);
    std::printf("NOTE     modeparam: %zu released rows in %s\n", rows.size(), (fixtures / "modeparam.tsv").c_str());

    // ---- every slot through the host path, the normalised table, the development session format ------------------------
    std::int64_t slotBad = 0, normBad = 0, stateBad = 0;
    auto receiver = std::make_unique<fcmp::Processor>();
    for (int s = 0; s < kSlots; ++s)
    {
        const std::string_view want = expectedKey(s);
        const std::string_view got = playedKey(*proc, slotNorm(s));
        if (got != want)
        {
            std::printf("NOTE     modeparam.slot %d: plays %.*s, want %.*s\n", s, static_cast<int>(got.size()),
                        got.data(), static_cast<int>(want.size()), want.data());
            ++slotBad;
        }
        juce::RangedAudioParameter& m = proc->parameter(Pid::mode);
        const bool normOk = std::abs(static_cast<double>(m.getValue()) - static_cast<double>(s) / (kSlots - 1)) <= 1e-7
                         && proc->rawValue(Pid::mode) == static_cast<float>(s)
                         && std::abs(static_cast<double>(m.convertTo0to1(static_cast<float>(s))) - s / 127.0) <= 1e-7;
        normBad += normOk ? 0 : 1;

        // the receiver is never fresh: it keeps the previous iteration's Mode and a moved threshold
        receiver->parameter(Pid::thr).setValueNotifyingHost(0.1f + 0.8f * static_cast<float>(s % 7) / 7.0f);
        loadSlotOnly(*receiver, s);
        const std::string_view loaded = fcdsp::resolveSlot(receiver->currentRaw().modeSlot).key;
        stateBad += loaded == got ? 0 : 1;
    }
    P.eq("modeparam.slots.mismatches", slotBad, 0);
    P.eq("modeparam.norm.mismatches", normBad, 0);
    P.eq("modeparam.state.mismatches", stateBad, 0);

    // ---- candidate rows for the next release ---------------------------------------------------------------------------
    if (const std::optional<std::string> blessTo = argValue("--bless-to", false))
    {
        const fs::path dir = fs::path(*blessTo).parent_path() / "fixture-candidates";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path tmp = dir / "modeparam.tsv.tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << "# fixture candidates from proc.modeparam (never adopted automatically): append the rows of the Modes "
                   "a release ships to tests/fixtures/modeparam.tsv\n# stateVersion\tslot\tnorm\tkey\n";
            for (const fcdsp::ModeSlot& m : fcdsp::modeSlots())
            {
                char norm[32];
                std::snprintf(norm, sizeof norm, "%.9g", static_cast<double>(slotNorm(m.slot)));
                out << fcdsp::kStateVersion << '\t' << static_cast<int>(m.slot) << '\t' << norm << '\t' << m.key << '\n';
            }
        }
        fs::rename(tmp, dir / "modeparam.tsv", ec);
        if (ec)
            P.harnessError("proc.modeparam: cannot write " + (dir / "modeparam.tsv").string() + ": " + ec.message());
        else
            std::printf("NOTE     modeparam: candidates in %s\n", (dir / "modeparam.tsv").c_str());
    }
    return P.finish();
}
