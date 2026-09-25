// FCMP_PROBE layer=proc name=prefs scope=global timeout=60
//
// proc.prefs (P3, S12; 03 §3.5; 02 §5.9; HR Tools/PrefsCheck.cpp): the machine-wide UI preferences store
// (funkgui::UiPreferences, the file beside the theme) round-trips against a SCRATCH copy. The editor chooses the
// theme and its int preferences by clicking, which no headless harness can do, so this exercises the store directly.
// The store reads FCMP_PREFS_DIR (FunkGui's <ENV_PREFIX>PREFS_DIR) at its first get(): CTest points it at
// <build>/sandbox/proc.prefs (ProbeMain empties that first); run by hand without it, the probe makes a fresh temporary
// directory before the first get(), so it never reads or writes the user's real file.
//
// Rows (spec, 0/1; 03 §3.5: golden "-"):
//   prefs.sandboxed                   the store's file is <FCMP_PREFS_DIR>/preferences.settings, not the real one
//   prefs.theme.write_reaches_disk    setTheme(a different theme) is on disk at once (the early-out on an unchanged
//                                     value would otherwise make this a test of nothing, as HR's first version was)
//   prefs.theme.clamp_in_range        setTheme(99) selects a theme that exists
//   prefs.reload_sees_other_process   another process's write, read back by reload(), with a revision bump
//   prefs.int.<key>.write_reaches_disk  each int preference FCompressor's editor keeps (meterScaleDb, historySpanTenths,
//                                     uiZoom; rows meter_scale_db, history_span_tenths, ui_zoom): setInt(a value
//                                     different from the one held) is on disk at once
//   prefs.int.<key>.read_back         getInt returns it; clamped to the caller's bounds
//   prefs.int.missing_fallback        a key the file lacks reads as the fallback, clamped
//   prefs.int.garbage_fallback        a value that is not a decimal integer reads as the fallback
//   prefs.int.same_value_no_bump      setInt of the value already held changes nothing (revision unchanged)
#include "ProbeRegistry.h"

#include <funkgui/core/Theme.h>
#include <funkgui/prefs/UiPreferences.h>

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
    namespace T = funkgui::test;

    constexpr const char* kPrefsDirVar = "FCMP_PREFS_DIR";

    // The val of the <VALUE name=key .../> the file holds, or "" when it has none.
    juce::String diskValue(const juce::File& file, const juce::String& key)
    {
        if (const std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(file))
            for (const juce::XmlElement* e : xml->getChildIterator())
                if (e->hasTagName("VALUE") && e->getStringAttribute("name") == key)
                    return e->getStringAttribute("val");
        return {};
    }

    // Rewrites the file as another process would: every entry it holds, with `key` set to `value`.
    bool writeAsOtherProcess(const juce::File& file, const juce::String& key, const juce::String& value)
    {
        juce::XmlElement root("PROPERTIES");
        bool found = false;
        if (const std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(file))
            for (const juce::XmlElement* e : xml->getChildIterator())
                if (e->hasTagName("VALUE"))
                {
                    juce::XmlElement* v = root.createNewChildElement("VALUE");
                    v->setAttribute("name", e->getStringAttribute("name"));
                    const bool isKey = e->getStringAttribute("name") == key;
                    v->setAttribute("val", isKey ? value : e->getStringAttribute("val"));
                    found = found || isKey;
                }
        if (!found)
        {
            juce::XmlElement* v = root.createNewChildElement("VALUE");
            v->setAttribute("name", key);
            v->setAttribute("val", value);
        }
        return root.writeTo(file);
    }

    struct IntKey
    {
        const char* key;
        const char* row;                                         // the key as a row name (lower case)
        int lo, hi, a, b;                                        // bounds; two distinct legal values
    };

    // The int preferences FCompressor's editor keeps beside the theme (02 §5.9): the TRANSFER plot's meter scale
    // (TransferPlot.cpp: 12/24/48/72 dB), the history span in tenths of a second (HistoryPlot.cpp: 25/50/100/200) and
    // the UI zoom (UF1b, EditorConfig::zoomPrefKey: 100/125/150/175 %).
    constexpr IntKey kIntKeys[] = {
        { "meterScaleDb", "meter_scale_db", 12, 72, 24, 72 },
        { "historySpanTenths", "history_span_tenths", 25, 200, 50, 200 },
        { "uiZoom", "ui_zoom", 100, 175, 125, 150 },
    };
} // namespace

FCMP_PROBE(proc, prefs)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // Before the singleton's first get(), which reads the variable once.
    juce::File scratch;
    const char* dirEnv = std::getenv(kPrefsDirVar);
    if (dirEnv == nullptr || *dirEnv == '\0')
    {
        scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("fcmp-proc-prefs-" + juce::String(juce::Time::currentTimeMillis()));
        scratch.createDirectory();
        T::setEnv(kPrefsDirVar, scratch.getFullPathName().toRawUTF8());
        dirEnv = std::getenv(kPrefsDirVar);
    }
    const juce::File expected = juce::File(juce::String::fromUTF8(dirEnv)).getChildFile("preferences.settings");
    funkgui::UiPreferences& prefs = funkgui::UiPreferences::get();
    const juce::File file = prefs.file();

    const bool sandboxed = file == expected && file != funkgui::UiPreferences::defaultFile();
    P.eq("prefs.sandboxed", sandboxed ? 1 : 0, 1);
    std::printf("NOTE     prefs.file %s\n", file.getFullPathName().toRawUTF8());
    if (!sandboxed)
    {
        P.harnessError("proc.prefs: the preferences store is not the scratch file; refusing to write the real one");
        return P.finish();
    }

    // ---- the theme ----------------------------------------------------------------------------------------------------
    const int start = prefs.theme();
    const int other = (start + 1) % funkgui::Theme::kCount;
    prefs.setTheme(other);
    P.eq("prefs.theme.write_reaches_disk", diskValue(file, "theme") == juce::String(other) ? 1 : 0, 1);
    prefs.setTheme(99);
    P.eq("prefs.theme.clamp_in_range", prefs.theme() >= 0 && prefs.theme() < funkgui::Theme::kCount ? 1 : 0, 1);
    {
        const std::uint32_t rev = prefs.revision();
        const bool wrote = writeAsOtherProcess(file, "theme", juce::String(start));
        prefs.reload();
        P.eq("prefs.reload_sees_other_process", wrote && prefs.theme() == start && prefs.revision() != rev ? 1 : 0, 1);
    }

    // ---- the int preferences ------------------------------------------------------------------------------------------
    for (const IntKey& k : kIntKeys)
    {
        const std::string key = std::string("prefs.int.") + k.row;
        const int held = prefs.getInt(k.key, k.a, k.lo, k.hi);
        const int want = held == k.a ? k.b : k.a;
        prefs.setInt(k.key, want);
        P.eq(key + ".write_reaches_disk", diskValue(file, k.key) == juce::String(want) ? 1 : 0, 1);
        const bool readBack = prefs.getInt(k.key, k.lo, k.lo, k.hi) == want
                           && prefs.getInt(k.key, k.lo, k.lo, want - 1) == want - 1;   // clamped to the bounds
        P.eq(key + ".read_back", readBack ? 1 : 0, 1);
    }
    {
        const int got = prefs.getInt("fcmpProbeMissingKey", 500, 100, 175);
        P.eq("prefs.int.missing_fallback", got == 175 ? 1 : 0, 1);
    }
    {
        const bool wrote = writeAsOtherProcess(file, "fcmpProbeGarbage", "12abc");
        prefs.reload();
        P.eq("prefs.int.garbage_fallback", wrote && prefs.getInt("fcmpProbeGarbage", 125, 100, 175) == 125 ? 1 : 0, 1);
    }
    {
        const IntKey& k = kIntKeys[0];
        const int held = prefs.getInt(k.key, k.a, k.lo, k.hi);
        const std::uint32_t rev = prefs.revision();
        prefs.setInt(k.key, held);
        P.eq("prefs.int.same_value_no_bump", prefs.revision() == rev ? 1 : 0, 1);
    }

    if (scratch.exists())
        scratch.deleteRecursively();
    return P.finish();
}
