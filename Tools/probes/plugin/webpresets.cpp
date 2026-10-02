// FCMP_PROBE layer=proc name=webpresets scope=global timeout=120
//
// proc.webpresets (web Sprint C, ADR-93): the browser demo's PresetAccess (web/facade/WebPresets, a session list with
// no JUCE, database or file) against the plugin's (a Processor's presets(): FunkPresets over the sandboxed store,
// $FCMP_PRESETS_DB). Every call is made on both through ProcessorFacade, and after each one the two are compared:
//   <op>.result             both returned what the row expects (true: done; false: refused)
//   <op>.revision           both revision()s moved (done) or held (refused)
//   <op>.state.mismatches   0: the list (count; per row name, category, modeKey and factory; the uuid of factory rows:
//                           a user preset's uuid is each side's own), current(), modified(), whether a preset is
//                           current, and the 30 raw values, bit for bit
// Spec rows only.
//
//   fresh                   the factory bank in bank order, Init current, unmodified
//   apply.NN                every factory row in turn (the seven globals a preset does not hold are set off their
//                           defaults first): apply.globals_kept, and apply.records: one Params record with the snap
//                           each
//   nudge.*                 a write one float step away leaves the preset unmodified; THRESHOLD -1 dB, a step of an
//                           index parameter and a Mode change each read modified
//   step.*                  +1 and -1, the wrap at both ends, and from no preset (+1: the first row, -1: the last)
//   save.*                  saveAs: names made unique ignoring case and against the factory names ("a" beside "A" is
//                           "a 3" when "a 2" exists), trimmed, an empty one refused; the saved preset is current,
//                           unmodified; the order of "b", "A", "a 10", "a 2"
//   rename.*, overwrite.*, remove.*   user rows only; a taken or empty name, a factory row and no row are refused;
//                           removing the current preset leaves it untitled
//   applyuser               a user preset through the host map (the raw values equal on both sides)
//   restore.*               restoreCurrent: a factory uuid, a user uuid (each side's own), "", an unknown one, and the
//                           uuid already current (nothing: the revision holds)
//   history.*               the edit history carries the identity: a preset, undo, redo
//   files.*                 the web has no files: importFile and exportFile refuse and change nothing
#include "ProbeRegistry.h"

#include "LoopbackLink.h"

#include "plugin/Processor.h"
#include "plugin/ProcessorFacade.h"
#include "plugin/portable/FactoryData.h"

#include "web/facade/WebFacade.h"

#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/params/ParamPort.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace
{
    using fcdsp::Pid;
    using funkgui::test::Probe;
    using fcmp::PresetAccess;
    using fcmp::ProcessorFacade;

    bool sameBits(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

    // By hand (no CTest sandbox): never the user's own presets, nor the machine's new-instance preferences.
    void sandbox()
    {
        const char* db = std::getenv("FCMP_PRESETS_DB");
        const char* prefs = std::getenv("FCMP_PREFS_DIR");
        if (db != nullptr && *db != '\0' && prefs != nullptr && *prefs != '\0')
            return;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path dir = std::filesystem::temp_directory_path()
                                        / ("fcmp-proc-webpresets-" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        funkgui::test::setEnv("FCMP_PRESETS_DB", (dir / "presets.db").string().c_str());
        funkgui::test::setEnv("FCMP_PREFS_DIR", dir.string().c_str());
        std::printf("NOTE     no sandbox in the environment: presets and preferences under %s\n", dir.string().c_str());
    }

    void tap01(ProcessorFacade& f, Pid pid, float v01)           // one editor gesture
    {
        funkgui::ParamPort& p = f.port(pid);
        p.beginGesture();
        p.setValue01(v01);
        p.endGesture();
    }

    void tap(ProcessorFacade& f, Pid pid, float plain) { tap01(f, pid, fcdsp::toNorm(pid, plain)); }

    constexpr Pid kGlobals[] = { Pid::extkey, Pid::listen, Pid::delta, Pid::bypass, Pid::quality, Pid::labudget,
                                 Pid::output };

    struct Pair
    {
        PresetAccess& a() { return proc.presets(); }
        PresetAccess& b() { return facade.presets(); }

        std::int64_t stateMismatches()
        {
            std::int64_t bad = a().count() == b().count() ? 0 : 1;
            std::set<std::string> uuids;
            for (int i = 0; i < std::min(a().count(), b().count()); ++i)
            {
                const PresetAccess::Row ra = a().row(i), rb = b().row(i);
                bad += ra.name == rb.name && ra.category == rb.category && ra.modeKey == rb.modeKey
                       && ra.factory == rb.factory ? 0 : 1;
                // A factory row's uuid is the bank's; a user row's is the side's own: there, not empty, never the
                // factory pattern, and no other row's.
                bad += ra.factory ? (ra.uuid == rb.uuid ? 0 : 1)
                                  : (!rb.uuid.empty() && !rb.uuid.starts_with("00000000-") ? 0 : 1);
                bad += uuids.insert(rb.uuid).second ? 0 : 1;
            }
            bad += a().current() == b().current() ? 0 : 1;
            bad += a().modified() == b().modified() ? 0 : 1;
            bad += a().currentUuid().empty() == b().currentUuid().empty() ? 0 : 1;
            if (const int c = b().current(); c >= 0)
                bad += b().row(c).uuid == b().currentUuid() ? 0 : 1;
            for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
                bad += sameBits(proc.rawValue(static_cast<Pid>(i)), facade.plain(static_cast<Pid>(i))) ? 0 : 1;
            return bad;
        }

        // One call on both, then the three rows. fn(ProcessorFacade&) returns what the call returned (true for a
        // call without a result).
        template <class Fn>
        void op(Probe& P, const std::string& name, bool done, Fn&& fn)
        {
            const std::uint32_t revA = a().revision(), revB = b().revision();
            const bool okA = fn(static_cast<ProcessorFacade&>(proc));
            const bool okB = fn(static_cast<ProcessorFacade&>(facade));
            const bool movedA = a().revision() != revA, movedB = b().revision() != revB;
            P.eq(name + ".result", okA == done && okB == done ? 1 : 0, 1);
            P.eq(name + ".revision", movedA == done && movedB == done ? 1 : 0, 1);
            P.eq(name + ".state.mismatches", stateMismatches(), 0);
        }

        // A write that is not a preset call: the state rows only.
        template <class Fn>
        void edit(Probe& P, const std::string& name, Fn&& fn)
        {
            fn(static_cast<ProcessorFacade&>(proc));
            fn(static_cast<ProcessorFacade&>(facade));
            P.eq(name + ".state.mismatches", stateMismatches(), 0);
        }

        int rowNamed(std::string_view name)                      // the same index on both, or -1
        {
            int at = -1;
            for (int i = 0; i < b().count(); ++i)
                if (b().row(i).name == name)
                    at = i;
            return at >= 0 && at < a().count() && a().row(at).name == name ? at : -1;
        }

        fcmp::Processor           proc;
        fcmp::probe::LoopbackLink link;
        fcmp::web::WebFacade      facade{ link };
    };

    std::string two(int i)
    {
        char text[16];
        std::snprintf(text, sizeof text, "%02d", i);
        return text;
    }
} // namespace

FCMP_PROBE(proc, webpresets)
{
    (void) C;
    sandbox();
    const juce::ScopedJuceInitialiser_GUI juceInit;              // this thread is the message thread
    Pair r;
    const int nFactory = static_cast<int>(fcmp::factory::factoryRows().size());

    // ---- fresh: the bank, Init, unmodified --------------------------------------------------------------------------
    P.eq("fresh.count", r.b().count(), nFactory);
    P.eq("fresh.init_current", r.b().current() == 0 && !r.b().modified() && r.b().row(0).name == "Init"
                               && r.b().currentUuid() == r.b().row(0).uuid ? 1 : 0, 1);
    P.eq("fresh.out_of_range_row", r.b().row(-1).uuid.empty() && r.b().row(r.b().count()).name.empty() ? 1 : 0, 1);
    P.eq("fresh.state.mismatches", r.stateMismatches(), 0);

    // ---- every factory row ------------------------------------------------------------------------------------------
    r.edit(P, "globals", [](ProcessorFacade& f) {
        tap01(f, Pid::extkey, 1.0f);
        tap01(f, Pid::listen, 1.0f);
        tap01(f, Pid::delta, 1.0f);
        tap01(f, Pid::bypass, 1.0f);
        tap(f, Pid::quality, 2.0f);
        tap(f, Pid::labudget, 1.0f);
        tap(f, Pid::output, -4.5f);
    });
    float globals[std::size(kGlobals)];
    for (std::size_t g = 0; g < std::size(kGlobals); ++g)
        globals[g] = r.facade.plain(kGlobals[g]);
    r.link.clearLog();
    std::int64_t globalsMoved = 0;
    for (int i = 0; i < nFactory; ++i)
    {
        r.op(P, "apply." + two(i), true, [i](ProcessorFacade& f) {
            f.presets().apply(i);
            return true;
        });
        for (std::size_t g = 0; g < std::size(kGlobals); ++g)
            globalsMoved += sameBits(globals[g], r.facade.plain(kGlobals[g])) ? 0 : 1;
    }
    P.eq("apply.globals_kept", globalsMoved, 0);
    P.eq("apply.records", r.link.log().params == nFactory && r.link.log().snapped() == nFactory ? 1 : 0, 1);
    P.eq("apply.all_unmodified_current_last", r.b().current() == nFactory - 1 && !r.b().modified() ? 1 : 0, 1);
    r.op(P, "apply.out_of_range", false, [](ProcessorFacade& f) {
        const int before = f.presets().current();
        f.presets().apply(-1);
        f.presets().apply(f.presets().count());
        return f.presets().current() != before;                  // nothing happened: "refused"
    });

    // ---- modified() -------------------------------------------------------------------------------------------------
    r.op(P, "nudge.init", true, [](ProcessorFacade& f) {
        f.presets().apply(0);
        return true;
    });
    r.edit(P, "nudge.one_ulp", [](ProcessorFacade& f) {
        funkgui::ParamPort& p = f.port(Pid::mix);
        p.beginGesture();
        p.setValue01(std::nextafter(p.value01(), 0.0f));
        p.endGesture();
    });
    P.eq("nudge.one_ulp.unmodified", !r.a().modified() && !r.b().modified() ? 1 : 0, 1);
    r.edit(P, "nudge.threshold", [](ProcessorFacade& f) { tap(f, Pid::thr, f.currentRaw()[Pid::thr] - 1.0f); });
    P.eq("nudge.threshold.modified", r.a().modified() && r.b().modified() ? 1 : 0, 1);
    r.op(P, "nudge.reapply", true, [](ProcessorFacade& f) {
        f.presets().apply(0);
        return true;
    });
    r.edit(P, "nudge.index", [](ProcessorFacade& f) { tap(f, Pid::det, 1.0f); });
    P.eq("nudge.index.modified", r.a().modified() && r.b().modified() ? 1 : 0, 1);
    r.op(P, "nudge.reapply2", true, [](ProcessorFacade& f) {
        f.presets().apply(0);
        return true;
    });
    r.edit(P, "nudge.mode", [](ProcessorFacade& f) { tap(f, Pid::mode, 1.0f); });
    P.eq("nudge.mode.modified", r.a().modified() && r.b().modified() ? 1 : 0, 1);

    // ---- step -------------------------------------------------------------------------------------------------------
    const auto step = [](int d) {
        return [d](ProcessorFacade& f) {
            f.presets().step(d);
            return true;
        };
    };
    r.op(P, "step.first", true, [](ProcessorFacade& f) {
        f.presets().apply(0);
        return true;
    });
    r.op(P, "step.back_wraps", true, step(-1));
    P.eq("step.back_wraps.last", r.b().current(), r.b().count() - 1);
    r.op(P, "step.forward_wraps", true, step(1));
    P.eq("step.forward_wraps.first", r.b().current(), 0);
    r.op(P, "step.two", true, step(2));
    P.eq("step.two.at", r.b().current(), 2);
    r.op(P, "step.zero", false, [](ProcessorFacade& f) {
        const int before = f.presets().current();
        f.presets().step(0);
        return f.presets().current() != before;
    });

    // ---- saveAs: names, order, identity -----------------------------------------------------------------------------
    const auto save = [](std::string_view name, std::string_view category) {
        return [name, category](ProcessorFacade& f) { return f.presets().saveAs(name, category); };
    };
    r.edit(P, "save.sound", [](ProcessorFacade& f) {
        tap(f, Pid::thr, -31.0f);
        tap(f, Pid::makeup, 2.5f);
    });
    r.op(P, "save.b", true, save("b", "Cat"));
    {
        const PresetAccess::Row saved = r.b().row(nFactory);
        P.eq("save.b.current_unmodified", r.b().current() == nFactory && !r.b().modified() && saved.category == "Cat"
                                          && !saved.factory ? 1 : 0, 1);
    }
    r.edit(P, "save.sound2", [](ProcessorFacade& f) { tap(f, Pid::thr, -12.0f); });
    r.op(P, "save.upper_a", true, save("A", ""));
    r.op(P, "save.a_10", true, save("a 10", " Spaced "));
    r.op(P, "save.a_2", true, save("a 2", "x"));
    {
        const bool order = r.b().count() == nFactory + 4 && r.b().row(nFactory).name == "A"
                           && r.b().row(nFactory + 1).name == "a 2" && r.b().row(nFactory + 2).name == "a 10"
                           && r.b().row(nFactory + 3).name == "b" && r.b().row(nFactory + 2).category == "Spaced";
        P.eq("save.order", order ? 1 : 0, 1);
    }
    r.op(P, "save.lower_a", true, save("a", ""));                // "A" has it, "a 2" too: "a 3"
    P.eq("save.lower_a.name", r.rowNamed("a 3") >= 0 ? 1 : 0, 1);
    r.op(P, "save.a_2_again", true, save("a 2", ""));            // continues at "a 3", taken: "a 4"
    P.eq("save.a_2_again.name", r.rowNamed("a 4") >= 0 ? 1 : 0, 1);
    r.op(P, "save.factory_name", true, save("init", ""));        // a factory preset has it: "init 2"
    P.eq("save.factory_name.name", r.rowNamed("init 2") >= 0 ? 1 : 0, 1);
    r.op(P, "save.trimmed", true, save("  Trim Me \t", "  Cat  "));
    P.eq("save.trimmed.name", r.rowNamed("Trim Me") >= 0 && r.b().row(r.rowNamed("Trim Me")).category == "Cat" ? 1 : 0,
         1);
    r.op(P, "save.empty", false, save("", "x"));
    r.op(P, "save.blank", false, save(" \t ", ""));

    // ---- rename -----------------------------------------------------------------------------------------------------
    const auto rename = [](int index, std::string_view name) {
        return [index, name](ProcessorFacade& f) { return f.presets().rename(index, name); };
    };
    r.op(P, "rename.current", true, rename(r.rowNamed("Trim Me"), "zz top"));   // the current preset: it follows
    P.eq("rename.current.follows", r.b().current() == r.rowNamed("zz top") && r.b().current() == r.b().count() - 1
                                   ? 1 : 0, 1);
    r.op(P, "rename.own_name_other_case", true, rename(r.rowNamed("b"), "B"));
    r.op(P, "rename.taken", false, rename(r.rowNamed("B"), "a"));               // "A" has it
    r.op(P, "rename.taken_by_factory", false, rename(r.rowNamed("B"), "INIT"));
    r.op(P, "rename.empty", false, rename(r.rowNamed("B"), "  "));
    r.op(P, "rename.factory_row", false, rename(1, "Mine"));
    r.op(P, "rename.no_row", false, rename(-1, "Mine"));
    r.op(P, "rename.past_end", false, rename(r.b().count(), "Mine"));
    r.op(P, "rename.reorders", true, rename(r.rowNamed("a 10"), "0 first"));
    P.eq("rename.reorders.first_user_row", r.rowNamed("0 first"), nFactory);

    // ---- overwrite --------------------------------------------------------------------------------------------------
    const auto overwrite = [](int index) {
        return [index](ProcessorFacade& f) { return f.presets().overwrite(index); };
    };
    r.edit(P, "overwrite.sound", [](ProcessorFacade& f) {
        tap(f, Pid::mode, 2.0f);
        tap(f, Pid::ratio, 0.5f);
    });
    P.eq("overwrite.sound.modified", r.b().modified() ? 1 : 0, 1);
    const int target = r.rowNamed("B");
    r.op(P, "overwrite.user", true, overwrite(target));
    {
        const std::string_view liveKey = fcdsp::resolveSlot(r.facade.currentRaw().modeSlot).key;
        P.eq("overwrite.user.current_unmodified", r.b().current() == target && !r.b().modified()
                                                  && r.b().row(target).modeKey == liveKey ? 1 : 0, 1);
    }
    r.op(P, "overwrite.factory_row", false, overwrite(0));
    r.op(P, "overwrite.no_row", false, overwrite(r.b().count()));

    // ---- a user preset applied --------------------------------------------------------------------------------------
    r.op(P, "applyuser.other", true, [i = r.rowNamed("A")](ProcessorFacade& f) {
        f.presets().apply(i);
        return true;
    });
    r.op(P, "applyuser.overwritten", true, [target](ProcessorFacade& f) {
        f.presets().apply(target);
        return true;
    });
    P.eq("applyuser.overwritten.unmodified", !r.a().modified() && !r.b().modified() ? 1 : 0, 1);

    // ---- restoreCurrent ---------------------------------------------------------------------------------------------
    const auto restore = [](int index) {                         // the row's uuid, each side's own
        return [index](ProcessorFacade& f) {
            const bool before = f.presets().current() == index;
            f.presets().restoreCurrent(f.presets().row(index).uuid);
            return f.presets().current() == index && !before;
        };
    };
    r.op(P, "restore.factory", true, restore(3));
    r.op(P, "restore.same", false, restore(3));                  // already current: nothing, the revision holds
    r.op(P, "restore.user", true, restore(r.rowNamed("a 2")));
    r.op(P, "restore.none", true, [](ProcessorFacade& f) {
        f.presets().restoreCurrent("");
        return f.presets().current() == -1 && !f.presets().modified();
    });
    r.op(P, "restore.none_again", false, [](ProcessorFacade& f) {
        f.presets().restoreCurrent("");
        return false;
    });
    r.op(P, "restore.back", true, restore(1));
    r.op(P, "restore.unknown", true, [](ProcessorFacade& f) {
        f.presets().restoreCurrent("no-such-preset");
        return f.presets().current() == -1 && !f.presets().modified();
    });

    // ---- step from no preset ----------------------------------------------------------------------------------------
    r.op(P, "step.from_none_forward", true, step(1));
    P.eq("step.from_none_forward.first", r.b().current(), 0);
    r.op(P, "step.to_none", true, [](ProcessorFacade& f) {
        f.presets().restoreCurrent("");
        return true;
    });
    r.op(P, "step.from_none_back", true, step(-1));
    P.eq("step.from_none_back.last", r.b().current(), r.b().count() - 1);

    // ---- remove -----------------------------------------------------------------------------------------------------
    const auto remove = [](int index) { return [index](ProcessorFacade& f) { return f.presets().remove(index); }; };
    r.edit(P, "remove.sound", [](ProcessorFacade& f) { tap(f, Pid::knee, 20.0f); });
    r.op(P, "remove.current", true, remove(r.b().current()));    // the last row, current and modified
    P.eq("remove.current.untitled", r.b().current() == -1 && r.b().currentUuid().empty() && r.b().modified() ? 1 : 0,
         1);
    r.op(P, "remove.factory_row", false, remove(2));
    r.op(P, "remove.no_row", false, remove(-3));
    r.op(P, "remove.past_end", false, remove(r.b().count()));
    {
        const int keep = r.rowNamed("B"), gone = r.rowNamed("0 first");   // a row before the current one: it follows
        r.op(P, "remove.before", true, [keep, gone](ProcessorFacade& f) {
            f.presets().apply(keep);
            return f.presets().remove(gone);
        });
    }
    const int rowB = r.rowNamed("B");
    P.eq("remove.before.current_follows", r.b().current() == rowB && rowB >= nFactory ? 1 : 0, 1);

    // ---- the edit history carries the identity ----------------------------------------------------------------------
    r.op(P, "history.preset", true, [](ProcessorFacade& f) {
        f.presets().apply(4);
        return f.edits().canUndo();
    });
    P.eq("history.preset.same_name", std::string(r.proc.edits().undoName()) == r.facade.edits().undoName() ? 1 : 0, 1);
    r.op(P, "history.undo", true, [rowB](ProcessorFacade& f) {
        return f.edits().undo() && f.presets().current() == rowB;
    });
    r.op(P, "history.redo", true, [](ProcessorFacade& f) {
        return f.edits().redo() && f.presets().current() == 4 && !f.presets().modified();
    });

    // ---- no files on the web ----------------------------------------------------------------------------------------
    {
        const std::uint32_t rev = r.b().revision();
        const int count = r.b().count();
        const bool refused = !r.b().importFile("/tmp/fcmp-webpresets.fcmppreset")
                          && !r.b().exportFile(0, "/tmp/fcmp-webpresets.fcmppreset");
        P.eq("files.refused", refused && r.b().revision() == rev && r.b().count() == count ? 1 : 0, 1);
    }
    P.eq("link.refused", r.link.log().refused, 0);
    return P.finish();
}
