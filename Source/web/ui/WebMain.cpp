// Source/web/ui/WebMain.cpp: the editor module's main() (web Sprint D, ADR-93): gpu/Editor's counterpart in a browser.
// The same fcmp::ui::Panel, over a WebFacade (web/facade/WebFacade.h) whose EngineLink is a PortLink (PortLink.h) to
// the engine module in the page's AudioWorklet, drawn by a funkgui::WebHost on the page's canvas.
//
// The page's side is the seam of docs/sprints/web-d.md. Before fcmp-ui.js loads the page defines
// `var Module = { fcmpReady() {...}, onAbort(what) {...} }` and holds canvas#fcmp-canvas; main() then gives it
//   Module.fcmpPort.connect(port, sampleRate, maxBlock), .disconnect()   PortLink's
//   Module.fcmpStatus()       a JSON text: ok, error, frames, fps, zoom (the host's), replies, refused, flags, latency,
//                             prepared, rate (the facade's), posted, dropped (the link's), host (the browser's name);
//                             and, beyond the seam, carriers, ignored (the link's) and lost (frames a lost context ate)
//   Module.fcmpSelftest()     a JSON text: atlasHash, FontService's hash as 16 hex digits (ui.font's font.atlas.hash),
//                             and pixels {frames, largest, over2, samples}: one frame drawn through the host's sink
//                             and read back in the same call (the page cannot read the canvas afterwards: the browser
//                             clears the drawing buffer once it has presented it), against SoftRaster's image of the
//                             same PrimList (where the device pixel ratio gave the canvas a rounded side, both are
//                             drawn from that frame at the nearest proportional size: drawAndCompare below). frames
//                             is 1 when that frame was drawn and read, else 0; largest is the largest channel
//                             difference, over2 the samples that differ by more than 2, samples the channel samples
//                             compared
//   Module.fcmpResetEngine()  WebFacade::resetEngine(): a new source
//   Module.fcmpShutdown()     the teardown below. Every name above stays callable afterwards: the port and the reset
//                             do nothing, the status says `ok` 0 with the reason, the self-test draws no frame
// and calls Module.fcmpReady() last (Emscripten runs Module.onRuntimeInitialized before main(), so that is not the
// page's signal). Query parameters: view (a ViewSpec id, as FCMP_UI_VIEW) and the capture pins theme, zoom, dt, scale.
//
// What main() sets up, in order (gpu/Editor.cpp is the native counterpart):
// - Preferences in localStorage, before the Panel exists: a Panel reads them as it is built (keys "FCompressor.<key>").
// - The link and the facade; the facade's environment is "WEB" and the browser's name (the settings screen's FORMAT).
// - ADR-85's QUALITY and LOOKAHEAD for new instances, applied as the processor's constructor applies them (a stored
//   value outside the three choices is ignored): the page load is the new instance, and the settings rows write the
//   two preferences into localStorage.
// - The Panel with the asynchronous preview: there is no thread here, so PreviewWorker's no-thread path computes a
//   request once it has rested, and a control that moves stays smooth.
// - The WebHost: the native editor's zoom steps, default and preference key; fit margins measured from where the
//   canvas lies on the page (what must fit in the window is the canvas, not what the page holds below it);
//   setUiAttached forwarded; no batch hooks (the Panel's HostProxy already brackets the facade, Editor.h); beforeTick
//   pulls the telemetry, so a Pull follows the host's cadence (60 Hz, 12 Hz idle, none while the document is hidden).
// - The DISPLAY row's facts (RenderInfo): "WEBGL2", no overflow count (a lost context is not a full buffer).
//
// Teardown (Editor::~Editor's order, then the members the native editor does not own): the render-info source goes,
// Panel::shutdown() (the preview stops, then the gestures close), then the host (the clock, the listeners,
// setUiAttached(false), the sink), the Panel, the facade, the link. It runs from Module.fcmpShutdown() and on a
// pagehide that is not persisted: a page kept in the back/forward cache comes back alive and must still tick.
//
// EM_JS bodies are C string literals to the preprocessor: no trailing semicolon after the macro (-Wextra-semi), no
// apostrophe in a comment, no regex literal with a backslash.
#include "web/ui/PortLink.h"

#include "web/facade/WebFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/Pid.h"

#include <funkgui/canvas/SoftRaster.h>
#include <funkgui/core/Theme.h>
#include <funkgui/prefs/UiPreferences.h>
#include <funkgui/text/FontService.h>
#include <funkgui/web/WebHost.h>
#include <funkgui/web/WebPrefs.h>

#include <emscripten/em_js.h>
#include <emscripten/em_macros.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>

using FcmpUiTextFn = const char* (*)(void);
using FcmpUiVoidFn = void (*)(void);

EM_JS_DEPS(fcmp_ui_deps, "$UTF8ToString,$stringToUTF8,$getWasmTableEntry")

EM_JS(int, fcmp_ui_param, (const char* name, char* out, int size), {
    const value = new URLSearchParams(window.location.search).get(UTF8ToString(name));
    stringToUTF8(value === null ? "" : value, out, size);
    return value === null ? 0 : 1;
})

// The browser's name and major version: the brand the browser gives where it gives one, else from the user agent.
// The brand list has no order to rely on (Chromium permutes it by version) and holds a made-up entry ("Not?A_Brand",
// "Not A;Brand": GREASE) that is never the browser: the name is the first brand that is neither that nor "Chromium"
// (the browser's own: Google Chrome, Microsoft Edge); otherwise "Chromium" itself where it is listed (a Chromium with
// no brand of its own); otherwise the list says nothing and the user agent is read.
EM_JS(void, fcmp_ui_browser, (char* out, int size), {
    const data = navigator.userAgentData;
    let name = "";
    if (data && data.brands) {
        const grease = new RegExp("Not.A.Brand");
        const named = Array.from(data.brands).filter((b) => b && typeof b.brand === "string" && !grease.test(b.brand));
        const brand = named.find((b) => b.brand !== "Chromium") || named.find((b) => b.brand === "Chromium");
        if (brand) name = brand.brand + " " + brand.version;
    }
    if (!name) {
        const m = navigator.userAgent.match(new RegExp("(Firefox|Version|Chrome)/([0-9]+)"));
        name = m ? (m[1] === "Version" ? "Safari" : m[1]) + " " + m[2] : "";
    }
    stringToUTF8(name, out, size);
})

// The page around the canvas, in CSS px, for the zoom's fit: what must fit in the window is the canvas. Across: what
// lies left of it, twice (the same again on its right). Down: what lies above it, and under it a margin as wide as the
// one on its left; what the page holds below the canvas is not counted (it is scrolled to). No element of the page is
// named. A known limit: the host's margins are fixed numbers, so this is measured once, as main() runs, and a page
// that loads in a narrow window (its header wrapped, the canvas lower) keeps that larger margin when it is widened.
EM_JS(int, fcmp_ui_margin, (const char* selector, int vertical), {
    const canvas = document.querySelector(UTF8ToString(selector));
    if (!canvas) return 0;
    const box = canvas.getBoundingClientRect();
    const left = box.left + window.scrollX;
    return Math.ceil(vertical ? box.top + window.scrollY + left : 2 * left);
})

// The page-facing names, the pagehide rule, and last the page's own signal.
EM_JS(void, fcmp_ui_ready,
      (FcmpUiTextFn status, FcmpUiTextFn selftest, FcmpUiVoidFn resetEngine, FcmpUiVoidFn shutdown),
{
    const statusText = getWasmTableEntry(status);
    const selftestText = getWasmTableEntry(selftest);
    const reset = getWasmTableEntry(resetEngine);
    const shutDown = getWasmTableEntry(shutdown);
    Module['fcmpStatus'] = () => UTF8ToString(statusText());
    Module['fcmpSelftest'] = () => UTF8ToString(selftestText());
    Module['fcmpResetEngine'] = () => { reset(); };
    Module['fcmpShutdown'] = () => { shutDown(); };
    window.addEventListener("pagehide", (event) => { if (!event.persisted) shutDown(); });
    if (Module['fcmpReady']) Module['fcmpReady']();
})

namespace
{
    constexpr const char* kCanvas = "#fcmp-canvas";
    constexpr const char* kZoomPrefKey = "uiZoom";       // gpu/Editor.cpp's: UiPreferences, beside the theme (ADR-68)
    constexpr const char* kRenderer = "WEBGL2";          // RenderInfo::renderer: WebGlSink draws with nothing else
    constexpr int kPixelTolerance = 2;                   // fcmpSelftest's over2

    struct App final : fcmp::web::PortLink::Events
    {
        fcmp::web::PortLink  link{ this };
        fcmp::web::WebFacade facade{ link };
        std::unique_ptr<fcmp::ui::Panel>  panel;
        std::unique_ptr<funkgui::WebHost> host;

        // The page handed over the worklet's port: what its engine runs at, then the values and the attach.
        void connected(double sampleRate, int maxBlock) override
        {
            facade.setEngineSetup(sampleRate, maxBlock);
            facade.resync();
        }
        void disconnected() override {}                  // the facade keeps what it last heard; the Panel goes stale
    };

    std::unique_ptr<App> app;                            // the runtime outlives main(); null again after the shutdown
    std::unique_ptr<std::string> text;                   // what fcmpStatus and fcmpSelftest last returned

    double numberParam(const char* name, double lo, double hi, double otherwise)
    {
        char value[64];
        if (fcmp_ui_param(name, value, static_cast<int>(sizeof value)) == 0 || value[0] == '\0')
            return otherwise;
        char* end = nullptr;
        const double v = std::strtod(value, &end);
        return end != value && *end == '\0' && v >= lo && v <= hi ? v : otherwise;
    }

    // `s` as a JSON string: a shader log or a browser's brand may hold quotes, backslashes and line ends.
    void appendJson(std::string& out, std::string_view s)
    {
        out += '"';
        for (const char c : s)
        {
            if (c == '"' || c == '\\')
            {
                out += '\\';
                out += c;
            }
            else if (static_cast<unsigned char>(c) < 0x20)
            {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += escaped;
            }
            else
                out += c;
        }
        out += '"';
    }

    const char* statusText()
    {
        std::string& out = *text;
        out = "{\"ok\":";
        if (app == nullptr)
        {
            out += "0,\"error\":\"the editor was shut down\",\"frames\":0,\"fps\":0.0,\"zoom\":100,\"replies\":0,"
                   "\"refused\":0,\"flags\":0,\"latency\":0,\"prepared\":0,\"rate\":0,\"posted\":0,\"dropped\":0,"
                   "\"host\":\"\",\"carriers\":0,\"ignored\":0,\"lost\":0}";
            return out.c_str();
        }
        const funkgui::WebHost::Diagnostics d = app->host->diagnostics();
        const fcmp::Diagnostics f = app->facade.diagnostics();
        const fcmp::web::PortLink::Counters& c = app->link.counters();
        char numbers[256];
        out += app->host->ok() ? "1,\"error\":" : "0,\"error\":";
        appendJson(out, app->host->error());
        std::snprintf(numbers, sizeof numbers,
                      ",\"frames\":%u,\"fps\":%.1f,\"zoom\":%d,\"replies\":%u,\"refused\":%u,\"flags\":%u,"
                      "\"latency\":%d,\"prepared\":%d,\"rate\":%.0f,\"posted\":%u,\"dropped\":%u,\"host\":",
                      d.frames, static_cast<double>(d.fps), d.zoomPercent, app->facade.replies(),
                      app->facade.repliesRefused(), app->facade.replyFlags(), f.latencySamples, f.prepared ? 1 : 0,
                      f.sampleRate, c.posted, c.dropped);
        out += numbers;
        appendJson(out, f.host);
        std::snprintf(numbers, sizeof numbers, ",\"carriers\":%u,\"ignored\":%u,\"lost\":%u}", c.carriers, c.ignored,
                      d.lost);
        out += numbers;
        return out.c_str();
    }

    struct Pixels
    {
        unsigned    frames = 0;                          // 1: a frame was drawn, read back and compared
        int         largest = 0;                         // the largest channel difference
        std::size_t over2 = 0, samples = 0;              // samples over kPixelTolerance; samples compared
    };

    // One frame through the host (a tick, the Panel's draw, the sink) and, in the same task, the canvas's pixels
    // against SoftRaster's image of the list that frame recorded, at one sample per pixel as the sink draws.
    //
    // The two are comparable only where they scale alike. SoftRaster scales both axes by info.dpi, which the host
    // sets to the buffer's height over the Panel's; the sink stretches the Panel over the whole buffer, so across it
    // scales by the buffer's width over the Panel's. That is one number while the buffer is proportional to the Panel
    // (the canvas's CSS size times the device pixel ratio is whole both ways: ratio 1, 1.25, 1.5, 2). Where a side
    // was rounded (ratio 1.3333: 1280 x 853 for a 960 x 640 Panel) the sink's picture is up to half a pixel narrower
    // or wider than SoftRaster's by its right edge, every edge on the way differs by most of a channel's range, and
    // the difference says nothing about the sink. A proportional frame is judged then: a copy of the list with its
    // dpi set for the largest height not above the buffer's that gives a whole proportional size (an even height here,
    // the width 3/2 of it) goes through the same sink at exactly that size and is read back, and SoftRaster draws the
    // same copy: one list and one scale for both. The host's own list is then submitted again at the host's size, so
    // the canvas presents the frame it would have.
    //
    // What the two really draw differently stays in the verdict. Measured in Chrome (ANGLE on Metal): a frame
    // recorded below about 0.8 device px per logical px (a browser zoomed far out) differs by a few levels in some
    // tens of samples, at exact ratios too; and on the characteristics screen about 2 inexact ratios in 100 leave one
    // sample one level over the tolerance.
    Pixels drawAndCompare(funkgui::WebHost& host)
    {
        Pixels p;
        if (!host.frame(emscripten_performance_now()).submitted)
            return p;                                    // a hidden document, no context, or a lost one
        const funkgui::FontAtlasSdf& atlas = funkgui::FontService::get().atlas();
        const funkgui::PrimList& frame = host.lastFrame();
        const funkgui::WebHost::Diagnostics d = host.diagnostics();
        const int logicalW = frame.info.logicalW, logicalH = frame.info.logicalH;
        if (logicalW <= 0 || logicalH <= 0 || d.physW <= 0 || d.physH <= 0)
            return p;
        funkgui::Image gl, soft;
        if (static_cast<long long>(d.physW) * logicalH == static_cast<long long>(d.physH) * logicalW)
        {
            gl = host.sink().readPixels();
            soft = funkgui::rasterise(frame, atlas, 1);
        }
        else
        {
            const int unit = std::gcd(logicalW, logicalH);
            const int n = d.physH / (logicalH / unit);   // the buffer: n times the Panel's size in lowest terms
            const int w = n * (logicalW / unit), h = n * (logicalH / unit);
            funkgui::PrimList copy = frame;
            copy.info.dpi = static_cast<float>(h) / static_cast<float>(logicalH);
            if (n > 0 && host.sink().submit(copy, w, h) == funkgui::WebGlSink::Result::submitted)
            {
                gl = host.sink().readPixels();
                soft = funkgui::rasterise(copy, atlas, 1);
            }
            host.sink().submit(frame, d.physW, d.physH);
        }
        if (gl.w <= 0 || gl.h <= 0 || gl.w != soft.w || gl.h != soft.h)
            return p;                                    // nothing was read back, or not at the size asked for
        const std::size_t samples = static_cast<std::size_t>(gl.w) * static_cast<std::size_t>(gl.h) * 4u;
        if (gl.rgba.size() != samples || soft.rgba.size() != samples)
            return p;
        for (std::size_t i = 0; i < samples; ++i)
        {
            const int delta = std::abs(static_cast<int>(gl.rgba[i]) - static_cast<int>(soft.rgba[i]));
            p.largest = std::max(p.largest, delta);
            if (delta > kPixelTolerance)
                ++p.over2;
        }
        p.samples = samples;
        p.frames = 1;
        return p;
    }

    const char* selftestText()
    {
        funkgui::FontService& fonts = funkgui::FontService::get();
        fonts.atlas();                                   // the bake, when nothing has drawn yet
        const Pixels p = app != nullptr ? drawAndCompare(*app->host) : Pixels{};
        char json[192];
        std::snprintf(json, sizeof json,
                      "{\"atlasHash\":\"%016llx\",\"pixels\":{\"frames\":%u,\"largest\":%d,\"over2\":%zu,"
                      "\"samples\":%zu}}",
                      static_cast<unsigned long long>(fonts.atlasHash()), p.frames, p.largest, p.over2, p.samples);
        *text = json;
        return text->c_str();
    }

    void resetEngine()
    {
        if (app != nullptr)
            app->facade.resetEngine();
    }

    void shutdown()
    {
        if (app == nullptr)
            return;
        app->panel->setRenderInfo({});
        app->panel->shutdown();                          // the preview stops, then the gestures close
        app->host.reset();                               // the clock, the listeners, setUiAttached(false), the sink
        app->panel.reset();
        app.reset();                                     // the facade, then the link: Module.fcmpPort goes quiet
    }
}

int main()
{
    // Preferences first: a Panel reads them as it is built (WebPrefs.h).
    funkgui::installLocalStoragePrefs(nullptr);

    text = std::make_unique<std::string>();
    app = std::make_unique<App>();
    char browser[sizeof(fcmp::Diagnostics::host)] = {};
    fcmp_ui_browser(browser, static_cast<int>(sizeof browser));
    app->facade.setEnvironment("WEB", browser);

    // ADR-85: a new instance starts from the machine's QUALITY and LOOKAHEAD for new instances, when set
    // (Processor's constructor). Here the machine is this browser's localStorage and the instance is the page load.
    // As there (newInstancePref), a stored value that is not one of the three choices leaves the default: it is read
    // over the whole range of an int, since getInt clamps what it reads into the range it is given.
    for (const auto& [pid, key] : { std::pair{ fcdsp::Pid::quality, fcmp::kPrefNewQuality },
                                    std::pair{ fcdsp::Pid::labudget, fcmp::kPrefNewLookahead } })
        if (const int v = funkgui::UiPreferences::get().getInt(key, -1, INT_MIN, INT_MAX); v >= 0 && v <= 2)
            app->facade.port(pid).setValue01(static_cast<float>(v) / 2.0f);

    fcmp::ui::PanelOptions options;
    options.syncPreview = false;                         // no thread: a request is computed once it has rested
    app->panel = std::make_unique<fcmp::ui::Panel>(app->facade, options);
    char view[32];
    if (fcmp_ui_param("view", view, static_cast<int>(sizeof view)) != 0)
        if (const fcmp::ui::ViewSpec* v = fcmp::ui::findView(view))
            app->panel->setView(*v, /*instant*/ true);   // before the first tick, as the native editor does

    funkgui::WebHostConfig config;
    config.canvasSelector = kCanvas;
    config.zoomSteps.assign(fcmp::ui::layout::footer::kZoomSteps.begin(), fcmp::ui::layout::footer::kZoomSteps.end());
    config.defaultZoomPercent = fcmp::ui::layout::footer::kDefaultZoomPercent;
    config.zoomPrefKey = kZoomPrefKey;
    config.fitMarginX = fcmp_ui_margin(kCanvas, 0);
    config.fitMarginY = fcmp_ui_margin(kCanvas, 1);
    config.setUiAttached = [](bool on) { app->facade.setUiAttached(on); };
    config.beforeTick = [] { app->facade.pull(); };      // one Pull per frame that ticks, before the tick
    config.capture.uiTheme = static_cast<int>(numberParam("theme", 0.0, funkgui::Theme::kCount - 1, -1.0));
    config.capture.uiZoom = static_cast<int>(numberParam("zoom", 25.0, 400.0, 0.0));
    config.capture.fixedDt = static_cast<float>(numberParam("dt", 1.0e-6, 1.0, 0.0));
    config.capture.uiScale = static_cast<float>(numberParam("scale", 0.25, 8.0, 0.0));
    app->host = std::make_unique<funkgui::WebHost>(*app->panel, std::move(config));

    // ADR-85: the settings screen's DISPLAY row reads the host's own diagnostics (shutdown() removes the source
    // before the host goes).
    app->panel->setRenderInfo([] {
        const funkgui::WebHost::Diagnostics d = app->host->diagnostics();
        fcmp::ui::RenderInfo r;
        r.gpu = app->host->ok();
        r.displayLinked = app->host->running();
        r.fps = d.fps;
        r.scale = d.scale;
        r.zoomPercent = d.zoomPercent;
        r.frames = d.frames;
        r.overflows = 0;
        r.renderer = kRenderer;
        return r;
    });
    app->host->start();
    fcmp_ui_ready(&statusText, &selftestText, &resetEngine, &shutdown);
    return 0;
}
