// ui.dump — visual inspection, not a test (03 §3.6; SPRINTS §0.2 [PNG]). This file deliberately has no
// "// FCMP_PROBE" first line, so CMake registers no CTest test for it; FCMP_PROBE(ui, dump) still registers the
// subcommand:
//
//   fcmp_probe_plugin ui.dump --mode <key> --golden-root <dir> --arch <arch> -- --view <id> --out <x.dump> [--png <x.png>]
//                                  [--dpi 1|2] [--theme 0|1]   (flags after "--" are ui.dump's own; S5 lead revision)
//
// It renders one view of fcmp::ui::views() for one Mode exactly as ui.geometry does (a FakeFacade, Panel{skipHint,
// syncPreview}, HeadlessHost, setView(instant), settle at 1/60 s), then writes the settled frame as dump v2 and, with
// --png, rasterises it with FunkGui's SoftRaster (2× supersampling). Defaults: --view panel, --mode clean, --dpi 2,
// --theme 0. `funkgui_framerender x.dump x.png 2` renders any dump the same way. Missing parent directories of the
// outputs are created.
//
// Exit: 0 written; 1 a usage error, an unknown view or Mode, an unsettled panel or a write error. ui.dump reads its
// own flags from the process arguments, which ProbeMain (frozen at FZ0) also hands to the harness: the harness prints
// "HARNESS ERROR unknown flag --view" (and asks for --golden-root / --arch) for them. Those lines are expected and do
// not change ui.dump's exit status, which it sets itself (std::exit) instead of returning finish()'s (U1a handoff:
// interface-change request for ProbeMain to strip a subcommand's own flags, as FunkGui's GalleryProbe does).
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Panel.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"

#include <funkgui/canvas/PrimList.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/text/FontService.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <crt_externs.h>                                         // _NSGetArgc / _NSGetArgv (macOS)

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    namespace ui = fcmp::ui;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;

    struct Args
    {
        std::string view = "panel";
        std::string mode = "clean";
        std::string out;
        std::string png;
        float       dpi = 2.0f;
        int         theme = 0;
        std::string error;                                       // non-empty: a usage error
    };

    Args parseArgs(std::string_view modeKey)
    {
        Args a;
        if (!modeKey.empty())
            a.mode = std::string(modeKey);
        const int argc = *_NSGetArgc();
        char** argv = *_NSGetArgv();
        for (int i = 2; i < argc; ++i)
        {
            const std::string_view flag = argv[i] != nullptr ? argv[i] : "";
            const bool hasValue = i + 1 < argc && argv[i + 1] != nullptr;
            const std::string value = hasValue ? argv[i + 1] : "";
            if (flag == "--view" || flag == "--out" || flag == "--png" || flag == "--dpi" || flag == "--theme")
            {
                if (!hasValue || value.empty() || value.starts_with("--"))
                {
                    a.error = std::string(flag) + " needs a value";
                    return a;
                }
                ++i;
                if (flag == "--view")
                    a.view = value;
                else if (flag == "--out")
                    a.out = value;
                else if (flag == "--png")
                    a.png = value;
                else if (flag == "--dpi")
                    a.dpi = value == "1" ? 1.0f : value == "2" ? 2.0f : 0.0f;
                else
                    a.theme = value == "0" ? 0 : value == "1" ? 1 : -1;
            }
        }
        if (a.out.empty())
            a.error = "--out <x.dump> is required";
        else if (!(a.dpi > 0.0f))
            a.error = "--dpi must be 1 or 2";
        else if (a.theme < 0)
            a.error = "--theme must be 0 or 1";
        return a;
    }

    bool makeParent(const std::string& path)
    {
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (parent.empty())
            return true;
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        return !ec;
    }

    int run(const Args& a)
    {
        if (!a.error.empty())
        {
            std::fprintf(stderr, "ui.dump: %s\nusage: fcmp_probe_plugin ui.dump --view <id> --mode <key> --out <x.dump> "
                                 "[--png <x.png>] [--dpi 1|2] [--theme 0|1]\n", a.error.c_str());
            return 1;
        }
        const ui::ViewSpec* view = ui::findView(a.view);
        if (view == nullptr)
        {
            std::fprintf(stderr, "ui.dump: unknown view '%s'; views:", a.view.c_str());
            for (const ui::ViewSpec& v : ui::views())
                std::fprintf(stderr, " %s", v.id);
            std::fprintf(stderr, "\n");
            return 1;
        }
        const fcdsp::ModeEntry* entry = fcdsp::byKey(a.mode);
        if (entry == nullptr)
        {
            std::fprintf(stderr, "ui.dump: unknown Mode '%s'\n", a.mode.c_str());
            return 1;
        }

        const juce::ScopedJuceInitialiser_GUI juceInit;          // FontService bakes the atlas through JUCE's fonts
        fcmp::probe::FakeFacade facade(a.mode);
        ui::Panel panel(facade, { true, true, false });
        funkgui::HeadlessHost host(panel, a.theme, a.dpi);
        panel.setView(*view, true);
        const int frames = host.settle(kMaxSettle, kDt);
        if (frames > kMaxSettle)
        {
            std::fprintf(stderr, "ui.dump: the panel did not settle within %d frames\n", kMaxSettle);
            return 1;
        }
        const funkgui::PrimList& pl = host.draw();
        if (!makeParent(a.out) || !host.writeDump(a.out.c_str()))
        {
            std::fprintf(stderr, "ui.dump: cannot write %s\n", a.out.c_str());
            return 1;
        }
        std::printf("ui.dump: %s  view %s  mode %s  dpi %g  theme %d  settled in %d frames  %zu prims  %u missing "
                    "glyphs\n", a.out.c_str(), view->id, a.mode.c_str(), static_cast<double>(a.dpi), a.theme, frames,
                    pl.prims.size(), pl.missingGlyphs);
        if (!a.png.empty())
        {
            if (!makeParent(a.png) || !host.writePng(a.png.c_str(), 2))
            {
                std::fprintf(stderr, "ui.dump: cannot write %s\n", a.png.c_str());
                return 1;
            }
            std::printf("ui.dump: %s\n", a.png.c_str());
        }
        return 0;
    }
}

FCMP_PROBE(ui, dump)
{
    std::printf("NOTE     ui.dump reads its own flags; the harness errors above about them are expected\n");
    const int rc = run(parseArgs(C.key));
    std::fflush(stdout);
    std::fflush(stderr);
    std::exit(rc);                                               // ui.dump's status, not finish()'s (see the top)
}
