// FCMP_PROBE layer=dsp name=registry scope=global timeout=60
//
// dsp.registry (F3, S2; 01 §8.3 is the canonical list; 03 §3.4; C §5.10.4; K1 #9, #20, #24; K2 #4, #9; K3 #9, #17): the
// registry lint. Spec rows only, never a golden (so adding a Mode rewrites no global file). Every row of 01 §8.3 except
// fb.monotone, which F9 (S3) adds with the first feedback kernel:
//
//   modesdef      the Modes.def FCMP_MODE / FCMP_RETIRED lines (parsed here with CMake's regex, FcmpSources.cmake)
//                 equal modeSlots() and retired(), in order: the CTest matrix and the C++ registry are the same list
//   keys/slots    keys unique and [a-z0-9-]{1,24}; slots unique and < 128; each registered key has its directory
//                 Source/fcdsp/modes/<key>/ and desc.key == key; retired keys have a registered successor; the lookups
//                 agree (bySlot, byKey, slotOf, resolveSlot, resolveKey); the entry fits the arena
//   modes-ever    no released slot or key (tests/fixtures/modes-ever.tsv) is reused or lost
//   apvts         kApvtsOrder is a permutation of every Pid (K2 #9)
//   steps         every step list strictly increasing, labels non-empty (> 6 glyphs: a WARNING note, the gate is
//                 ui.textfit); every non-live spec (not continuous/stepped) has a reason
//   variants      a variant's driver resolves earlier (kResolveOrder); derived specs never follow a derived spec
//   display       every DisplayMap round-trips within 1e-4 and `invert` matches the slope's sign
//   defaults      defaultPlain is a fixed point of snap()
//   pointers      detectorLaw, attackSpec, releaseSpec, tailSeconds; ModeEntry construct, staticGr, scShapeDb,
//                 colourCurve (physical may be null, K1 #30)
//   internals     <= 8, at most one history lane (K1 #20)
//   version       introducedInStateVersion <= kStateVersion; revision >= 1; no provisional Mode in a release build
//   crossmode     crossmode.no_off (K2 #4 ii): every ordered pair (A, B): modeDefaults(A) over the host defaults,
//                 resolved in B, has no kTagOff step, no kEngGrOff, and staticGr(T + 12 dB) > 0 unless B is
//                 limiter-only (Group::limit)
//   thr.slope     |dT_in/dthr - 1| <= 1e-4 at 5 points per Mode and per ratio step, T_in = analysis::inputThresholdDb
//                 (K1 #9)
//   goldens       every tests/golden/*/modes/<key>/ directory belongs to a registered or retired key
//
// On EVERY run it adds note("provisional", jsonArray(keys)) to the results JSON, "[]" when no Mode is provisional (S2
// lead revision 3): golden.py adopt refuses rows of those Modes, and every Mode-scoped candidate without the list.
//
// The source tree is found from this file's own path (__FILE__ is absolute under CMake): Modes.def, the fixture and
// tests/golden/ are read from the tree the probe was built from.
#include "ProbeRegistry.h"

#include "EngineRig.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/engine/IEngine.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/EngineParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifndef FCOMPRESSOR_RELEASE
  #define FCOMPRESSOR_RELEASE 0
#endif

namespace
{
    namespace fs = std::filesystem;
    using namespace fcdsp;
    using funkgui::test::Probe;

    constexpr std::array<const char*, kNumModeParams> kPidNames{
        "thr", "ratio", "knee", "range", "atk", "rel", "tmode", "hold", "look", "det", "schpf",
        "sce", "link", "stmode", "voice", "drive", "makeup", "automu", "mix", "s2thr", "s2atk", "s2rel" };

    const char* pidName(Pid p) { return kPidNames[idx(p)]; }
    std::string str(std::string_view s) { return std::string(s); }
    std::int64_t b2i(bool b) { return b ? 1 : 0; }

    fs::path sourceRoot()
    {
        fs::path p = fs::path(__FILE__);                                 // <root>/Tools/probes/dsp/registry.cpp
        return p.parent_path().parent_path().parent_path().parent_path();
    }

    // ---- Modes.def, as CMake parses it (cmake/FcmpSources.cmake) ----------------------------------------------------
    struct DefRow
    {
        int slot = 0;
        std::string key, traitsOrSuccessor;
        bool retired = false;
    };

    bool parseModesDef(const fs::path& file, std::vector<DefRow>& rows, std::string& error)
    {
        std::ifstream in(file);
        if (!in)
        {
            error = "cannot read " + file.string();
            return false;
        }
        const std::regex mode(
            R"re(^FCMP_MODE\( *([0-9]+) *, *"([a-z0-9-]+)" *, *([A-Za-z_][A-Za-z0-9_]*) *\) *(//.*)?$)re");
        const std::regex ret(R"re(^FCMP_RETIRED\( *([0-9]+) *, *"([a-z0-9-]+)" *, *"([a-z0-9-]+)" *\) *(//.*)?$)re");
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind("FCMP_", 0) != 0)
                continue;                                               // comments, reservations, blanks
            std::smatch m;
            if (std::regex_match(line, m, mode))
                rows.push_back({ std::stoi(m[1].str()), m[2].str(), m[3].str(), false });
            else if (std::regex_match(line, m, ret))
                rows.push_back({ std::stoi(m[1].str()), m[2].str(), m[3].str(), true });
            else
            {
                error = "malformed line: " + line;
                return false;
            }
        }
        return true;
    }

    bool validKey(std::string_view k)
    {
        if (k.empty() || k.size() > 24)
            return false;
        return std::all_of(k.begin(), k.end(),
                           [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
    }

    std::size_t glyphs(const char* s)                                   // UTF-8 code points
    {
        std::size_t n = 0;
        for (const char* p = s; *p != '\0'; ++p)
            if ((static_cast<unsigned char>(*p) & 0xc0u) != 0x80u)
                ++n;
        return n;
    }

    bool nonEmpty(const char* s) { return s != nullptr && *s != '\0'; }

    // Every spec of an entry: the base and each variant's.
    std::vector<const ParamSpec*> specsOf(const ParamEntry& e)
    {
        std::vector<const ParamSpec*> v{ &e.spec };
        for (const Variant& var : e.variants)
            v.push_back(&var.spec);
        return v;
    }

    int resolveOrderIndex(Pid p)
    {
        for (std::size_t i = 0; i < kResolveOrder.size(); ++i)
            if (kResolveOrder[i] == p)
                return static_cast<int>(i);
        return -1;
    }

    // ---- per-Mode descriptor rows -----------------------------------------------------------------------------------
    void descriptorRows(Probe& P, const ModeSlot& ms)
    {
        const ModeEntry& en = *ms.entry;
        const ModeDescriptor& d = *en.desc;
        const std::string k = "mode." + str(ms.key);

        // pointers (K1 #30: physical may be null)
        P.eq(k + ".ptr.detector_law", b2i(d.detectorLaw != nullptr), 1);
        P.eq(k + ".ptr.attack_spec", b2i(d.attackSpec != nullptr), 1);
        P.eq(k + ".ptr.release_spec", b2i(d.releaseSpec != nullptr), 1);
        P.eq(k + ".ptr.tail_seconds", b2i(d.tailSeconds != nullptr), 1);
        P.eq(k + ".ptr.construct", b2i(en.construct != nullptr), 1);
        P.eq(k + ".ptr.static_gr", b2i(en.staticGr != nullptr), 1);
        P.eq(k + ".ptr.sc_shape_db", b2i(en.scShapeDb != nullptr), 1);
        P.eq(k + ".ptr.colour_curve", b2i(en.colourCurve != nullptr), 1);
        P.le(k + ".engine_bytes", en.engineBytes, static_cast<double>(kArenaBytes));
        P.le(k + ".engine_align", en.engineAlign, 64);

        // internals (K1 #20)
        P.le(k + ".internals.count", static_cast<double>(d.internals.size()), 8);
        std::int64_t history = 0;
        for (const InternalSpec& s : d.internals)
            history += s.history ? 1 : 0;
        P.le(k + ".internals.history", static_cast<double>(history), 1);

        // version
        P.le(k + ".introduced_state_version", d.introducedInStateVersion, kStateVersion);
        P.ge(k + ".revision", d.revision, 1);
        if (FCOMPRESSOR_RELEASE != 0)
            P.eq(k + ".not_provisional_in_release", b2i(!d.provisional), 1);

        // parameters
        std::int64_t badSteps = 0, emptyLabels = 0, missingReasons = 0, longLabels = 0, badDrivers = 0;
        std::int64_t derivedOnDerived = 0, badDefaults = 0, displayBad = 0, invertBad = 0, displayMaps = 0;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
        {
            const Pid pid = static_cast<Pid>(i);
            const ParamEntry& e = d.params.e[i];
            if (e.driver != kNoPid || !e.variants.empty())
            {
                const int di = e.driver == kNoPid ? -1 : resolveOrderIndex(e.driver);
                const int pi = resolveOrderIndex(pid);
                if (di < 0 || pi < 0 || di >= pi)
                {
                    ++badDrivers;
                    std::printf("NOTE     %s %s: variant driver must resolve earlier (kResolveOrder)\n", ms.key.data(),
                                pidName(pid));
                }
            }
            for (const ParamSpec* s : specsOf(e))
            {
                for (std::size_t j = 0; j < s->steps.size(); ++j)
                {
                    const Step& st = s->steps[j];
                    if (j > 0 && !(st.plain > s->steps[j - 1].plain))
                        ++badSteps;
                    if (!nonEmpty(st.label))
                        ++emptyLabels;
                    else if (glyphs(st.label) > 6)
                    {
                        ++longLabels;
                        std::printf("NOTE     WARNING %s %s step label \"%s\" has %zu glyphs (> 6; ui.textfit is the "
                                    "gate)\n", ms.key.data(), pidName(pid), st.label, glyphs(st.label));
                    }
                }
                if (s->kind != Kind::continuous && s->kind != Kind::stepped && !nonEmpty(s->reason))
                {
                    ++missingReasons;
                    std::printf("NOTE     %s %s: a non-live spec without a reason\n", ms.key.data(), pidName(pid));
                }
                if (s->kind == Kind::derived)
                {
                    const bool validFrom = s->derivedFrom != kNoPid && idx(s->derivedFrom) < kNumModeParams;
                    bool onDerived = !validFrom || s->derive == nullptr;
                    if (validFrom)
                        for (const ParamSpec* f : specsOf(d.params.e[idx(s->derivedFrom)]))
                            onDerived = onDerived || f->kind == Kind::derived;
                    derivedOnDerived += onDerived ? 1 : 0;
                }
                else
                {
                    const float want = s->kind == Kind::locked || s->kind == Kind::notApplicable ? s->value
                                                                                                  : s->defaultPlain;
                    const Snapped sn = snap(pid, *s, s->defaultPlain);
                    if (!(sn.plain == want) || sn.clamped)
                    {
                        ++badDefaults;
                        std::printf("NOTE     %s %s: defaultPlain %.9g snaps to %.9g\n", ms.key.data(), pidName(pid),
                                    static_cast<double>(s->defaultPlain), static_cast<double>(sn.plain));
                    }
                }
                const DisplayMap& dm = s->display;
                if (dm.toDisplay != nullptr || dm.toPlain != nullptr)
                {
                    ++displayMaps;
                    if (dm.toDisplay == nullptr || dm.toPlain == nullptr)
                        ++displayBad;
                    else
                    {
                        std::vector<float> pts;
                        if (s->lo < s->hi)
                            for (int q = 0; q <= 16; ++q)
                                pts.push_back(s->lo + (s->hi - s->lo) * static_cast<float>(q) / 16.0f);
                        for (const Step& st : s->steps)
                            pts.push_back(st.plain);
                        for (const float p : pts)
                            if (!(std::fabs(dm.toPlain(dm.toDisplay(p)) - p) <= 1e-4f * std::fmax(1.0f, std::fabs(p))))
                                ++displayBad;
                        if (pts.size() >= 2)
                        {
                            const float a = *std::min_element(pts.begin(), pts.end());
                            const float b = *std::max_element(pts.begin(), pts.end());
                            const bool falls = dm.toDisplay(b) < dm.toDisplay(a);
                            invertBad += falls != dm.invert ? 1 : 0;
                        }
                    }
                }
            }
        }
        P.eq(k + ".steps.strictly_increasing_violations", badSteps, 0);
        P.eq(k + ".steps.empty_labels", emptyLabels, 0);
        P.eq(k + ".specs.missing_reasons", missingReasons, 0);
        P.eq(k + ".variants.driver_order_violations", badDrivers, 0);
        P.eq(k + ".derived.on_derived", derivedOnDerived, 0);
        P.eq(k + ".defaults.not_snap_fixed_points", badDefaults, 0);
        P.eq(k + ".display.round_trip_failures", displayBad, 0);
        P.eq(k + ".display.invert_mismatches", invertBad, 0);
        std::printf("NOTE     %s: %lld DisplayMap(s) checked, %lld step label(s) over 6 glyphs\n", ms.key.data(),
                    static_cast<long long>(displayMaps), static_cast<long long>(longLabels));
    }

    // thr.slope (K1 #9): T_in = thrDb - preGainDb has slope 1 in thr at 5 points per ratio step.
    void thrSlopeRows(Probe& P, const ModeSlot& ms)
    {
        const ModeEntry& en = *ms.entry;
        const ModeDescriptor& d = *en.desc;
        const std::string k = "thr.slope." + str(ms.key);
        RawParams raw = fcmp::probe::modeRaw(en);

        ParamView view;
        resolveView(d, raw, view);
        const ParamSpec* ratioSpec = view.spec[idx(Pid::ratio)];
        std::vector<float> ratios;
        if (ratioSpec != nullptr && ratioSpec->kind == Kind::stepped)
            for (const Step& st : ratioSpec->steps)
                ratios.push_back(st.plain);
        else
            ratios.push_back(raw[Pid::ratio]);

        const ParamSpec* thrSpec = view.spec[idx(Pid::thr)];
        if (thrSpec == nullptr || (thrSpec->kind != Kind::continuous && thrSpec->kind != Kind::hybrid
                                   && thrSpec->kind != Kind::stepped))
        {
            std::printf("NOTE     %s: thr is not live (kind %d); thr.slope skipped\n", ms.key.data(),
                        thrSpec == nullptr ? -1 : static_cast<int>(thrSpec->kind));
            return;
        }

        // 5 (thr_a, thr_b) pairs: +-0.25 dB around 5 interior points (continuous), or 5 adjacent-step pairs.
        std::vector<std::pair<float, float>> pairs;
        if (thrSpec->kind == Kind::stepped)
        {
            const std::size_t n = thrSpec->steps.size();
            for (std::size_t q = 0; q < 5 && n >= 2; ++q)
            {
                const std::size_t j = q * (n - 1) / 5;
                pairs.emplace_back(thrSpec->steps[j].plain, thrSpec->steps[j + 1].plain);
            }
        }
        else
            for (int q = 1; q <= 5; ++q)
            {
                const float c = thrSpec->lo + (thrSpec->hi - thrSpec->lo) * static_cast<float>(q) / 6.0f;
                pairs.emplace_back(c - 0.25f, c + 0.25f);
            }

        double worst = 0.0;
        for (const float ratio : ratios)
            for (const auto& [a, b] : pairs)
            {
                RawParams ra = raw, rb = raw;
                ra[Pid::ratio] = rb[Pid::ratio] = ratio;
                ra[Pid::thr] = a;
                rb[Pid::thr] = b;
                const Resolution za = fcmp::probe::resolveRaw(en, ra), zb = fcmp::probe::resolveRaw(en, rb);
                const double dIn = static_cast<double>(analysis::inputThresholdDb(zb.eng))
                                 - static_cast<double>(analysis::inputThresholdDb(za.eng));
                const double dThr = static_cast<double>(zb.view[Pid::thr].plain)
                                  - static_cast<double>(za.view[Pid::thr].plain);
                const double slope = dThr != 0.0 ? dIn / dThr : 0.0;
                worst = std::max(worst, std::fabs(slope - 1.0));
            }
        P.le(k + ".max_dev", worst, 1e-4);
    }

    // crossmode.no_off (K2 #4 ii) for the ordered pair (a, b).
    void crossmodeRows(Probe& P, const ModeSlot& a, const ModeSlot& b)
    {
        const std::string k = "crossmode.no_off." + str(a.key) + ".to." + str(b.key);
        RawParams raw = fcmp::probe::hostDefaults(*a.entry);
        modeDefaults(*a.entry->desc, raw);
        raw.modeSlot = b.slot;
        const Resolution res = fcmp::probe::resolveRaw(*b.entry, raw);
        std::int64_t offTags = 0;
        for (std::size_t i = 0; i < kNumModeParams; ++i)
            offTags += (res.view.p[i].tag & kTagOff) != 0 ? 1 : 0;
        P.eq(k + ".off_tags", offTags, 0);
        P.eq(k + ".gr_off_flag", b2i((res.eng.flags & kEngGrOff) != 0), 0);
        if (b.entry->desc->group != Group::limit)
        {
            const float x = res.eng.thrDb + 12.0f;                       // detector domain: T + 12 dB
            float gr = 0.0f;
            b.entry->staticGr(res.eng, &x, &gr, 1);
            P.ge(k + ".static_gr_t_plus_12_db", static_cast<double>(gr), 1e-6);
        }
    }
} // namespace

FCMP_PROBE(dsp, registry)
{
    const std::span<const ModeSlot> slots = modeSlots();
    const std::span<const Retired> retiredRows = retired();

    // ---- the provisional list: every run (S2 lead revision 3) -------------------------------------------------------
    std::vector<std::string> provisional;
    for (const ModeSlot& ms : slots)
        if (ms.entry != nullptr && ms.entry->desc != nullptr && ms.entry->desc->provisional)
            provisional.push_back(str(ms.key));
    P.note("provisional", funkgui::test::jsonArray(provisional));
    std::printf("NOTE     %zu registered Mode(s), %zu retired, %zu provisional\n", slots.size(), retiredRows.size(),
                provisional.size());

    const fs::path root = sourceRoot();
    if (!fs::exists(root / "Source/fcdsp/modes/Modes.def"))
    {
        P.harnessError("cannot locate the source tree from " + std::string(__FILE__));
        return P.finish();
    }

    // ---- Modes.def == the registry ----------------------------------------------------------------------------------
    std::vector<DefRow> def;
    std::string err;
    if (!parseModesDef(root / "Source/fcdsp/modes/Modes.def", def, err))
    {
        P.harnessError("Modes.def: " + err);
        return P.finish();
    }
    std::vector<DefRow> defModes, defRetired;
    for (const DefRow& r : def)
        (r.retired ? defRetired : defModes).push_back(r);
    P.eq("modesdef.mode_count", static_cast<std::int64_t>(defModes.size()), static_cast<std::int64_t>(slots.size()));
    P.eq("modesdef.retired_count", static_cast<std::int64_t>(defRetired.size()),
         static_cast<std::int64_t>(retiredRows.size()));
    std::int64_t defMismatch = 0;
    for (std::size_t i = 0; i < std::min(defModes.size(), slots.size()); ++i)
        defMismatch += defModes[i].slot != slots[i].slot || defModes[i].key != slots[i].key ? 1 : 0;
    for (std::size_t i = 0; i < std::min(defRetired.size(), retiredRows.size()); ++i)
        defMismatch += defRetired[i].slot != retiredRows[i].slot || defRetired[i].key != retiredRows[i].key
                           || defRetired[i].traitsOrSuccessor != retiredRows[i].successor
                       ? 1 : 0;
    P.eq("modesdef.rows_differ_from_registry", defMismatch, 0);

    // ---- keys and slots ---------------------------------------------------------------------------------------------
    std::set<std::string> keys;
    std::set<int> slotSet;
    std::int64_t badKeys = 0, dupKeys = 0, dupSlots = 0, badSlots = 0;
    const auto addKey = [&](std::string_view key, int slot) {
        badKeys += validKey(key) ? 0 : 1;
        dupKeys += keys.insert(str(key)).second ? 0 : 1;
        dupSlots += slotSet.insert(slot).second ? 0 : 1;
        badSlots += slot >= 0 && slot < kModeCapacity ? 0 : 1;
    };
    for (const ModeSlot& ms : slots)
        addKey(ms.key, ms.slot);
    for (const Retired& r : retiredRows)
        addKey(r.key, r.slot);
    P.eq("keys.invalid", badKeys, 0);
    P.eq("keys.duplicate", dupKeys, 0);
    P.eq("slots.duplicate", dupSlots, 0);
    P.eq("slots.out_of_range", badSlots, 0);
    std::int64_t orphanRetired = 0;
    for (const Retired& r : retiredRows)
        orphanRetired += byKey(r.successor) == nullptr ? 1 : 0;
    P.eq("retired.successor_not_registered", orphanRetired, 0);

    for (const ModeSlot& ms : slots)
    {
        const std::string k = "mode." + str(ms.key);
        if (ms.entry == nullptr || ms.entry->desc == nullptr)
        {
            P.eq(k + ".entry_and_descriptor", 0, 1);
            continue;
        }
        P.eq(k + ".desc_key_matches", b2i(ms.entry->desc->key == ms.key), 1);
        P.eq(k + ".directory_exists", b2i(fs::is_directory(root / "Source/fcdsp/modes" / str(ms.key))), 1);
        P.eq(k + ".by_slot", b2i(bySlot(ms.slot) == ms.entry), 1);
        P.eq(k + ".by_key", b2i(byKey(ms.key) == ms.entry), 1);
        P.eq(k + ".slot_of", slotOf(*ms.entry), ms.slot);
        P.eq(k + ".resolve_slot", b2i(resolveSlot(ms.slot).entry == ms.entry), 1);
        const ModeSlot* rk = resolveKey(ms.key);
        P.eq(k + ".resolve_key", b2i(rk != nullptr && rk->entry == ms.entry), 1);
        descriptorRows(P, ms);
    }
    if (const ModeEntry* clean = byKey("clean"); clean != nullptr)
    {
        std::int64_t wrong = 0;
        for (int s = 0; s < kModeCapacity; ++s)
            if (bySlot(s) == nullptr && std::none_of(retiredRows.begin(), retiredRows.end(),
                                                     [s](const Retired& r) { return r.slot == s; }))
                wrong += resolveSlot(s).entry == clean ? 0 : 1;
        P.eq("slots.unassigned_resolve_to_clean", wrong, 0);
    }

    // ---- modes-ever.tsv: released slots and keys are never reused or lost (C §5.10.4) -------------------------------
    {
        std::ifstream in(root / "tests/fixtures/modes-ever.tsv");
        if (!in)
            P.harnessError("cannot read tests/fixtures/modes-ever.tsv");
        std::int64_t rows = 0, lost = 0, moved = 0;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            const std::size_t tab = line.find('\t');
            if (tab == std::string::npos)
            {
                P.harnessError("modes-ever.tsv: malformed row: " + line);
                continue;
            }
            ++rows;
            const int slot = std::stoi(line.substr(0, tab));
            const std::string key = line.substr(tab + 1);
            bool found = false, sameSlot = false;
            for (const ModeSlot& ms : slots)
                if (ms.key == key)
                {
                    found = true;
                    sameSlot = ms.slot == slot;
                }
            for (const Retired& r : retiredRows)
                if (r.key == key)
                {
                    found = true;
                    sameSlot = r.slot == slot;
                }
            lost += found ? 0 : 1;
            moved += found && !sameSlot ? 1 : 0;
            for (const ModeSlot& ms : slots)                             // a released slot now holding another key
                moved += ms.slot == slot && ms.key != key ? 1 : 0;
        }
        std::printf("NOTE     modes-ever.tsv: %lld released row(s)\n", static_cast<long long>(rows));
        P.eq("modes_ever.lost", lost, 0);
        P.eq("modes_ever.reused_or_moved", moved, 0);
    }

    // ---- kApvtsOrder is a permutation of every Pid (K2 #9) ----------------------------------------------------------
    {
        std::array<int, kNumParams> seen{};
        for (const Pid p : kApvtsOrder)
            if (idx(p) < kNumParams)
                ++seen[idx(p)];
        std::int64_t bad = 0;
        for (const int c : seen)
            bad += c == 1 ? 0 : 1;
        P.eq("apvts_order.permutation_violations", bad, 0);
    }

    // ---- per Mode, and per ordered pair -----------------------------------------------------------------------------
    for (const ModeSlot& ms : slots)
        if (ms.entry != nullptr && ms.entry->desc != nullptr)
            thrSlopeRows(P, ms);
    for (const ModeSlot& a : slots)
        for (const ModeSlot& b : slots)
            if (a.entry != nullptr && b.entry != nullptr && a.entry->desc != nullptr && b.entry->desc != nullptr)
                crossmodeRows(P, a, b);

    // ---- golden directories belong to registered or retired keys (C §5.10.4) ----------------------------------------
    {
        std::int64_t orphans = 0;
        std::error_code ec;
        for (const auto& archDir : fs::directory_iterator(root / "tests/golden", ec))
        {
            const fs::path modes = archDir.path() / "modes";
            if (!fs::is_directory(modes, ec))
                continue;
            for (const auto& m : fs::directory_iterator(modes, ec))
                if (m.is_directory() && keys.count(m.path().filename().string()) == 0)
                {
                    ++orphans;
                    std::printf("NOTE     orphan golden directory %s\n", m.path().string().c_str());
                }
        }
        P.eq("goldens.orphan_mode_directories", orphans, 0);
    }
    return P.finish();
}
