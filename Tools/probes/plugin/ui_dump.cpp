// ui.dump — visual inspection, not a test (03 §3.6; SPRINTS §0.2 [PNG]). This file deliberately has no
// "// FCMP_PROBE" first line, so CMake registers no CTest test for it; FCMP_PROBE(ui, dump) still registers the
// subcommand:
//
//   fcmp_probe_plugin ui.dump --mode <key> --golden-root <dir> --arch <arch> -- --view <id> --out <x.dump> [--png <x.png>]
//                                  [--dpi 1|2] [--theme 0|1] [--wheel <points>] [--preset <name>] [--live <seconds>]
//                                  [--focus <view>:<local>] [--keys <spec>]
//                                  (flags after "--" are ui.dump's own; S5 lead revision)
//
// It renders one view of fcmp::ui::views() for one Mode exactly as ui.geometry does (a FakeFacade, Panel{skipHint,
// syncPreview}, HeadlessHost, setView(instant), settle at 1/60 s), then writes the settled frame as dump v2 and, with
// --png, rasterises it with FunkGui's SoftRaster (2× supersampling). Defaults: --view panel, --mode clean, --dpi 2,
// --theme 0. `funkgui_framerender x.dump x.png 2` renders any dump the same way. Missing parent directories of the
// outputs are created. --wheel (ADR-84) sends one trackpad scroll of that many points (> 0: the content moves up) at the
// overlay's centre after the view is set, to look at a list scrolled by the pixel.
// --focus (v1.2) gives the keyboard focus to the a11y id (ViewIndex number):(local), ring shown; --keys then sends
// HeadlessHost::keys(spec) (a typed-value field, ADR-89, is drawn open this way: --focus 2:1 --keys return, THRESHOLD:
// the slot grid is view 2, its slots are 1 + their layout::kSlots index).
// --preset (v1.2) loads the Mode's factory preset of that name (its values, and the strip shows it current). --live
// (v1.2, the README's screenshot) puts the Panel over EngineFacade, a real EngineHost, and plays a deterministic groove
// through it for that many seconds at 60 frames per second before the frame is written, so the meters, the GR readout,
// the history and the operating point show real signal. A live frame is written as it stands, without settling.
//
// Exit: 0 written; 1 a usage error, an unknown view or Mode, an unsettled panel or a write error. ui.dump reads its
// own flags from the process arguments, which ProbeMain (frozen at FZ0) also hands to the harness: the harness prints
// "HARNESS ERROR unknown flag --view" (and asks for --golden-root / --arch) for them. Those lines are expected and do
// not change ui.dump's exit status, which it sets itself (std::exit) instead of returning finish()'s (U1a handoff:
// interface-change request for ProbeMain to strip a subcommand's own flags, as FunkGui's GalleryProbe does).
#include "ProbeRegistry.h"

#include "EngineFacade.h"
#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "plugin/portable/FactoryData.h"
#include "web/facade/EngineLink.h"
#include "web/facade/WebFacade.h"
#include <funkgui/canvas/Fingerprint.h>
#include <funkgui/canvas/Tags.h>

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/canvas/PrimList.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/text/FontService.h>

#include <funkgui/panel/HeadlessGuiScope.h>


#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

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
        float       wheelPoints = 0.0f;                          // --wheel: one smooth wheel event, points
        std::string preset;                                      // --preset: a factory preset of the Mode
        float       liveSeconds = 0.0f;                          // --live: seconds of the groove through the engine
        std::string focus;                                       // --focus <view>:<local>
        std::string keys;                                        // --keys <HeadlessHost::keys spec>
        std::string facade = "fake";                             // --facade fake|web (PROTOTYPE)
        std::string host;                                        // --host <the browser name> (PROTOTYPE)
        std::string fp;                                          // --fp <x.fp>: the fingerprint lines (PROTOTYPE)
        bool        nolive = false;                              // --nolive 1 (PROTOTYPE)
        std::string error;                                       // non-empty: a usage error
    };


    // "<view>:<local>": both decimal, the view a ViewIndex.
    bool focusValid(const std::string& f)
    {
        const std::size_t colon = f.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= f.size())
            return false;
        for (std::size_t i = 0; i < f.size(); ++i)
            if (i != colon && (f[i] < '0' || f[i] > '9'))
                return false;
        return std::strtoul(f.substr(0, colon).c_str(), nullptr, 10) < static_cast<unsigned long>(ui::kSubViewCount);
    }
    Args parseArgs(std::string_view modeKey)
    {
        Args a;
        if (!modeKey.empty())
            a.mode = std::string(modeKey);
        const int argc = fcmp::probe::argc();
        char** argv = fcmp::probe::argv();
        for (int i = 2; i < argc; ++i)
        {
            const std::string_view flag = argv[i] != nullptr ? argv[i] : "";
            const bool hasValue = i + 1 < argc && argv[i + 1] != nullptr;
            const std::string value = hasValue ? argv[i + 1] : "";
            if (flag == "--view" || flag == "--out" || flag == "--png" || flag == "--dpi" || flag == "--theme"
                || flag == "--wheel" || flag == "--preset" || flag == "--live" || flag == "--focus"
                || flag == "--keys" || flag == "--facade" || flag == "--host" || flag == "--fp" || flag == "--nolive")
            {
                if (!hasValue || value.empty() || value.starts_with("--"))
                {
                    a.error = std::string(flag) + " needs a value";
                    return a;
                }
                ++i;
                if (flag == "--facade")
                    a.facade = value;
                else if (flag == "--host")
                    a.host = value;
                else if (flag == "--fp")
                    a.fp = value;
                else if (flag == "--nolive")
                    a.nolive = value == "1";
                else if (flag == "--view")
                    a.view = value;
                else if (flag == "--out")
                    a.out = value;
                else if (flag == "--png")
                    a.png = value;
                else if (flag == "--dpi")
                    a.dpi = value == "1" ? 1.0f : value == "2" ? 2.0f : 0.0f;
                else if (flag == "--wheel")
                    a.wheelPoints = std::strtof(value.c_str(), nullptr);
                else if (flag == "--preset")
                    a.preset = value;
                else if (flag == "--live")
                    a.liveSeconds = std::strtof(value.c_str(), nullptr);
                else if (flag == "--focus")
                    a.focus = value;
                else if (flag == "--keys")
                    a.keys = value;
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
        else if (!(a.liveSeconds >= 0.0f && a.liveSeconds <= 60.0f))
            a.error = "--live must be 0 to 60 seconds";
        else if (!a.focus.empty() && !focusValid(a.focus))
            a.error = "--focus must be <view 0-" + std::to_string(ui::kSubViewCount - 1) + ">:<local>";
        return a;
    }

    // PROTOTYPE: the page before START has no port: every record is dropped, no reply ever comes.
    struct NullLink final : fcmp::web::EngineLink
    {
        void post(std::span<const std::uint8_t>) override {}
        void setSink(fcmp::web::ReplySink*) override {}
    };

    bool writeFingerprint(const std::string& path, const funkgui::PrimList& l)
    {
        std::FILE* f = std::fopen(path.c_str(), "w");
        if (f == nullptr)
            return false;
        const funkgui::Fingerprint fp = funkgui::fingerprint(l);
        std::fprintf(f, "geometry %016llx\ntext %016llx\nstatics %d\nlive %d\ntexts %d\nrrects %d\nsegments %d\n"
                     "areas %d\nmax_x %.9g\nmax_y %.9g\n", static_cast<unsigned long long>(fp.geometry),
                     static_cast<unsigned long long>(fp.text), fp.statics, fp.live, fp.texts, fp.rrects, fp.segments,
                     fp.areas, static_cast<double>(fp.maxX), static_cast<double>(fp.maxY));
        for (const auto& [tag, count] : fp.tagCounts)
        {
            std::string name;
            if (const char* n = funkgui::tagName(tag))
                for (const char* c = n; *c != 0; ++c)
                    name += (*c >= 'A' && *c <= 'Z') ? static_cast<char>(*c - 'A' + 'a') : *c;
            else
                name = std::to_string(static_cast<unsigned>(tag));
            std::fprintf(f, "tag.%s %d\n", name.c_str(), count);
        }
        std::fprintf(f, "view_w %d\nview_h %d\nglyphs_missing %u\n", l.info.logicalW, l.info.logicalH, l.missingGlyphs);
        return std::fclose(f) == 0;
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

    // The factory preset `name` of Mode `mode`: its values written to the ports as a host would, and the strip's row
    // made current (FakePresets lists the whole bank in bank order). False when the bank has no such preset.
    bool applyPreset(fcmp::probe::FakeFacade& ports, const std::string& mode, const std::string& name)
    {
        const std::span<const fcmp::factory::FactoryRow> bank = fcmp::factory::factoryRows();
        for (std::size_t i = 0; i < bank.size(); ++i)
        {
            if (bank[i].name != name || bank[i].modeKey != mode)
                continue;
            for (const fcdsp::Pid pid : fcdsp::kApvtsOrder)      // what FactoryBank.cpp's preset holds, in its order
                if (fcdsp::idx(pid) < fcdsp::kNumModeParams && fcdsp::kHostParams[fcdsp::idx(pid)].inPresets)
                    ports.setPlain(pid, bank[i].values[fcdsp::idx(pid)]);
            ports.fakePresets().setCurrent(static_cast<int>(i));
            return true;
        }
        return false;
    }

    // A deterministic groove for --live: per half-second beat a low hit and a quieter high one, both decaying.
    fcmp::probe::Program groove(float seconds)
    {
        std::vector<fcmp::probe::Program::Tone> tones;
        for (float t = 0.0f; t < seconds; t += 0.5f)
        {
            tones.push_back({ 0.25, -4.0f, 70.0f, 36.0f });
            tones.push_back({ 0.25, -12.0f, 330.0f, 24.0f });
        }
        return fcmp::probe::Program(std::move(tones), fcmp::probe::EngineFacade::kFs);
    }

    int run(const Args& a)
    {
        if (!a.error.empty())
        {
            std::fprintf(stderr, "ui.dump: %s\nusage: fcmp_probe_plugin ui.dump --view <id> --mode <key> --out <x.dump> "
                                 "[--png <x.png>] [--dpi 1|2] [--theme 0|1] [--wheel <points>] [--preset <name>] "
                                 "[--live <seconds>] [--focus <view>:<local>] [--keys <spec>]\n", a.error.c_str());
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

        const funkgui::HeadlessGuiScope gui;                     // FontService bakes the atlas through JUCE's fonts
        std::unique_ptr<fcmp::probe::FakeFacade> fake;
        std::unique_ptr<fcmp::probe::EngineFacade> engine;
        NullLink nullLink;
        std::unique_ptr<fcmp::web::WebFacade> web;
        if (a.facade == "web")
        {
            web = std::make_unique<fcmp::web::WebFacade>(nullLink);
            web->setEnvironment("WEB", a.host);
        }
        else if (a.liveSeconds > 0.0f)
            engine = std::make_unique<fcmp::probe::EngineFacade>(a.mode, 1);
        else
            fake = std::make_unique<fcmp::probe::FakeFacade>(a.mode);
        fcmp::ProcessorFacade& facade = web ? static_cast<fcmp::ProcessorFacade&>(*web)
                                      : engine ? static_cast<fcmp::ProcessorFacade&>(*engine) : *fake;
        fcmp::probe::FakeFacade* ports = web ? nullptr : engine ? &engine->params() : fake.get();
        if (!a.preset.empty() && (ports == nullptr || !applyPreset(*ports, a.mode, a.preset)))
        {
            std::fprintf(stderr, "ui.dump: Mode '%s' has no factory preset '%s'\n", a.mode.c_str(), a.preset.c_str());
            return 1;
        }
        ui::Panel panel(facade, { true, true, a.nolive });
        funkgui::HeadlessHost host(panel, a.theme, a.dpi);
        panel.setView(*view, true);
        if (a.wheelPoints != 0.0f && std::isfinite(a.wheelPoints))
        {
            host.settle(kMaxSettle, kDt);                        // opened (a browser resets on its first tick)
            funkgui::WheelEvent w;
            w.x = ui::layout::kOverlay.centreX();
            w.y = ui::layout::kOverlay.centreY();
            w.dy = -a.wheelPoints / 512.0f;                      // JUCE on macOS: scrollingDeltaY / 512
            w.smooth = true;
            panel.wheel(w);
        }
        if (!a.focus.empty() || !a.keys.empty())
        {
            host.settle(kMaxSettle, kDt);
            if (const std::size_t colon = a.focus.find(':'); colon != std::string::npos)
            {
                const auto v = static_cast<uint32_t>(std::strtoul(a.focus.substr(0, colon).c_str(), nullptr, 10));
                const auto l = static_cast<uint32_t>(std::strtoul(a.focus.substr(colon + 1).c_str(), nullptr, 10));
                panel.a11yAction(ui::a11yId(static_cast<ui::ViewIndex>(v), l), funkgui::A11yAction::focus);
                host.tick(1, kDt);
            }
            if (!a.keys.empty())
                host.keys(a.keys.c_str());
        }
        int frames = 0;
        if (engine)
        {
            host.settle(kMaxSettle, kDt);                        // the view in place before the audio starts
            engine->setUiAttached(true);                         // the editor's lifetime gate (01 §6.3), as ui.truth
            const fcmp::probe::Program program = groove(a.liveSeconds);
            constexpr uint64_t kPerFrame = static_cast<uint64_t>(fcmp::probe::EngineFacade::kFs / 60.0);
            constexpr uint64_t kBlock = static_cast<uint64_t>(fcmp::probe::EngineFacade::kBlock);
            for (uint64_t target = kPerFrame; target <= program.length(); target += kPerFrame, ++frames)
            {
                if (target > engine->processed() + kBlock)
                    engine->render(program, static_cast<int>((target - engine->processed()) / kBlock));
                host.tick(1, kDt);
            }
        }
        else
            frames = host.settle(kMaxSettle, kDt);
        if (frames > kMaxSettle && !engine)
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
        if (!a.fp.empty() && !writeFingerprint(a.fp, pl))
            return 1;
        std::printf("ui.dump: %s  view %s  mode %s  dpi %g  theme %d  %s %d frames  %zu prims  %u missing glyphs\n",
                    a.out.c_str(), view->id, a.mode.c_str(), static_cast<double>(a.dpi), a.theme,
                    engine ? "live for" : "settled in", frames, pl.prims.size(), pl.missingGlyphs);
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
