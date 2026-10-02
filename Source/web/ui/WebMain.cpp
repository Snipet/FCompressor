// PROTOTYPE (scout-d): Source/web/ui/WebMain.cpp. The editor module's main(): WebFacade over PortLink, the Panel,
// funkgui::WebHost on the page's canvas.
#include "web/ui/PortLink.h"

#include "web/facade/WebFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "plugin/ProcessorFacade.h"

#include "fcdsp/params/Pid.h"

#include <funkgui/core/Theme.h>
#include <funkgui/prefs/UiPreferences.h>
#include <funkgui/web/WebHost.h>
#include <funkgui/web/WebPrefs.h>

#include <emscripten/em_js.h>
#include <emscripten/em_macros.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using FcmpUiTextFn = const char* (*)(void);
using FcmpUiVoidFn = void (*)(void);

EM_JS_DEPS(fcmp_ui_deps, "$UTF8ToString,$stringToUTF8,$getWasmTableEntry")

EM_JS(int, fcmp_ui_param, (const char* name, char* out, int size), {
    const value = new URLSearchParams(window.location.search).get(UTF8ToString(name));
    stringToUTF8(value === null ? "" : value, out, size);
    return value === null ? 0 : 1;
})

EM_JS(void, fcmp_ui_browser, (char* out, int size), {
    const data = navigator.userAgentData;
    let name = "";
    if (data && data.brands) {
        const brand = data.brands.find((b) => !b.brand.includes("Not") && b.brand !== "Chromium") || data.brands[0];
        if (brand) name = brand.brand + " " + brand.version;
    }
    if (!name) {
        const m = navigator.userAgent.match(new RegExp("(Firefox|Version|Chrome)/([0-9]+)"));
        name = m ? (m[1] === "Version" ? "Safari" : m[1]) + " " + m[2] : "";
    }
    stringToUTF8(name, out, size);
})

EM_JS(int, fcmp_ui_margin, (const char* selector, int vertical), {
    const canvas = document.querySelector(UTF8ToString(selector));
    if (!canvas) return 0;
    const box = canvas.getBoundingClientRect();
    return Math.ceil(2 * (vertical ? box.top + window.scrollY : box.left + window.scrollX));
})

EM_JS(void, fcmp_ui_ready, (FcmpUiTextFn status, FcmpUiVoidFn resetEngine, FcmpUiVoidFn shutdown), {
    const text = getWasmTableEntry(status);
    Module['fcmpStatus'] = () => UTF8ToString(text());
    Module['fcmpResetEngine'] = getWasmTableEntry(resetEngine);
    Module['fcmpShutdown'] = getWasmTableEntry(shutdown);
    if (Module['fcmpReady']) Module['fcmpReady']();
})

namespace
{
    constexpr const char* kCanvas = "#fcmp-canvas";
    constexpr const char* kZoomPrefKey = "uiZoom";

    // WebHost has no per-frame hook: the frame's pull rides on the Panel's tick (the option that needs no FunkGui
    // change). Everything else forwards.
    class FramePanel final : public funkgui::Panel
    {
    public:
        FramePanel(funkgui::Panel& inner, fcmp::web::WebFacade& facade) : inner_(inner), facade_(facade) {}
        void attach(funkgui::HostServices& h) override { inner_.attach(h); }
        int  width() const override { return inner_.width(); }
        int  height() const override { return inner_.height(); }
        void tick(float dt) override
        {
            facade_.pull();
            inner_.tick(dt);
        }
        void idle(double now) override { inner_.idle(now); }
        void draw(funkgui::Canvas& c, const funkgui::Theme& t) override { inner_.draw(c, t); }
        bool wantsFullRate() const override { return inner_.wantsFullRate(); }
        void pointerMove(const funkgui::PointerEvent& e) override { inner_.pointerMove(e); }
        void pointerExit() override { inner_.pointerExit(); }
        void pointerDown(const funkgui::PointerEvent& e) override { inner_.pointerDown(e); }
        void pointerDrag(const funkgui::PointerEvent& e) override { inner_.pointerDrag(e); }
        void pointerUp(const funkgui::PointerEvent& e) override { inner_.pointerUp(e); }
        void doubleClick(const funkgui::PointerEvent& e) override { inner_.doubleClick(e); }
        bool wheel(const funkgui::WheelEvent& e) override { return inner_.wheel(e); }
        bool key(const funkgui::KeyEvent& e) override { return inner_.key(e); }
        funkgui::Cursor cursor() const override { return inner_.cursor(); }
        void accessibility(std::vector<funkgui::A11yItem>& v) const override { inner_.accessibility(v); }
        uint32_t a11yRevision() const override { return inner_.a11yRevision(); }
        void a11yAction(uint32_t id, funkgui::A11yAction a, double v) override { inner_.a11yAction(id, a, v); }
        void closeGestures() override { inner_.closeGestures(); }

    private:
        funkgui::Panel&        inner_;
        fcmp::web::WebFacade&  facade_;
    };

    struct App final : fcmp::web::PortLink::Events
    {
        fcmp::web::PortLink  link{ this };
        fcmp::web::WebFacade facade{ link };
        std::unique_ptr<fcmp::ui::Panel>  panel;
        std::unique_ptr<FramePanel>       framed;
        std::unique_ptr<funkgui::WebHost> host;
        std::string status;

        void connected(double sampleRate, int maxBlock) override
        {
            facade.setEngineSetup(sampleRate, maxBlock);
            facade.resync();
        }
        void disconnected() override {}
    };

    std::unique_ptr<App> app;                            // the runtime outlives main()

    double numberParam(const char* name, double lo, double hi, double otherwise)
    {
        char text[64];
        if (fcmp_ui_param(name, text, static_cast<int>(sizeof text)) == 0 || text[0] == '\0')
            return otherwise;
        char* end = nullptr;
        const double v = std::strtod(text, &end);
        return end != text && *end == '\0' && v >= lo && v <= hi ? v : otherwise;
    }

    const char* statusText()
    {
        const funkgui::WebHost::Diagnostics d = app->host->diagnostics();
        const fcmp::Diagnostics f = app->facade.diagnostics();
        char text[512];
        std::snprintf(text, sizeof text,
                      "{\"ok\":%d,\"error\":\"%s\",\"frames\":%u,\"lost\":%u,\"fps\":%.1f,\"scale\":%.3g,\"zoom\":%d,"
                      "\"phys\":[%d,%d],\"replies\":%u,\"refused\":%u,\"flags\":%u,\"latency\":%d,\"prepared\":%d,"
                      "\"rate\":%.0f,\"written\":%.0f,\"posted\":%u,\"dropped\":%u,\"host\":\"%s\",\"format\":\"%s\"}",
                      app->host->ok() ? 1 : 0, app->host->error(), d.frames, d.lost, static_cast<double>(d.fps),
                      d.scale, d.zoomPercent, d.physW, d.physH, app->facade.replies(), app->facade.repliesRefused(),
                      app->facade.replyFlags(), f.latencySamples, f.prepared ? 1 : 0, f.sampleRate,
                      static_cast<double>(app->facade.history().written()), app->link.counters().posted,
                      app->link.counters().dropped, f.host, f.format);
        app->status = text;
        return app->status.c_str();
    }

    void resetEngine() { app->facade.resetEngine(); }

    void shutdown()
    {
        if (app == nullptr)
            return;
        if (app->panel != nullptr)
        {
            app->panel->setRenderInfo({});
            app->panel->shutdown();                      // the preview stops, then the gestures close
        }
        app->host.reset();                               // the clock, the listeners, setUiAttached(false), the sink
        app->framed.reset();
        app->panel.reset();
        app.reset();                                     // the facade, then the link
    }
}

int main()
{
    // Preferences first: a Panel reads them as it is built (WebPrefs.h). Keys are "FCompressor.<key>".
    funkgui::installLocalStoragePrefs(nullptr);

    app = std::make_unique<App>();
    char browser[sizeof(fcmp::Diagnostics::host)] = {};
    fcmp_ui_browser(browser, static_cast<int>(sizeof browser));
    app->facade.setEnvironment("WEB", browser);

    // ADR-85's machine-wide start values, which here are the browser's (localStorage).
    for (const auto& [pid, key] : { std::pair{ fcdsp::Pid::quality, fcmp::kPrefNewQuality },
                                    std::pair{ fcdsp::Pid::labudget, fcmp::kPrefNewLookahead } })
        if (const int v = funkgui::UiPreferences::get().getInt(key, -1, -1, 2); v >= 0)
            app->facade.port(pid).setValue01(static_cast<float>(v) / 2.0f);

    fcmp::ui::PanelOptions options;
    options.syncPreview = true;                          // no thread in this build
    app->panel = std::make_unique<fcmp::ui::Panel>(app->facade, options);
    char view[32];
    if (fcmp_ui_param("view", view, static_cast<int>(sizeof view)) != 0)
        if (const fcmp::ui::ViewSpec* v = fcmp::ui::findView(view))
            app->panel->setView(*v, /*instant*/ true);
    app->framed = std::make_unique<FramePanel>(*app->panel, app->facade);

    funkgui::WebHostConfig config;
    config.canvasSelector = kCanvas;
    config.zoomSteps.assign(fcmp::ui::layout::footer::kZoomSteps.begin(), fcmp::ui::layout::footer::kZoomSteps.end());
    config.defaultZoomPercent = fcmp::ui::layout::footer::kDefaultZoomPercent;
    config.zoomPrefKey = kZoomPrefKey;
    config.fitMarginX = fcmp_ui_margin(kCanvas, 0);
    config.fitMarginY = fcmp_ui_margin(kCanvas, 1);
    config.setUiAttached = [](bool on) { app->facade.setUiAttached(on); };
    config.capture.uiTheme = static_cast<int>(numberParam("theme", 0.0, funkgui::Theme::kCount - 1, -1.0));
    config.capture.uiZoom = static_cast<int>(numberParam("zoom", 25.0, 400.0, 0.0));
    config.capture.fixedDt = static_cast<float>(numberParam("dt", 1.0e-6, 1.0, 0.0));
    config.capture.uiScale = static_cast<float>(numberParam("scale", 0.25, 8.0, 0.0));
    app->host = std::make_unique<funkgui::WebHost>(*app->framed, std::move(config));

    app->panel->setRenderInfo([] {
        const funkgui::WebHost::Diagnostics d = app->host->diagnostics();
        fcmp::ui::RenderInfo r;
        r.gpu = app->host->ok();
        r.displayLinked = app->host->running();
        r.fps = d.fps;
        r.scale = d.scale;
        r.zoomPercent = d.zoomPercent;
        r.frames = d.frames;
        r.overflows = d.lost;
        r.renderer = "WEBGL2";
        return r;
    });
    app->host->start();
    fcmp_ui_ready(&statusText, &resetEngine, &shutdown);
    return 0;
}
