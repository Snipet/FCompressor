// Source/editor/gpu/Editor.cpp — see Editor.h (02 Part 2 intro, §5.1, §6.1; U7).
#include "editor/gpu/Editor.h"

#include "editor/Layout.h"

#include "FcmpProduct.h"

#include <funkgui/core/Env.h>
#include <funkgui/panel/CaptureConfig.h>

#include <memory>

namespace fcmp::ui
{
    namespace
    {
        constexpr const char* kZoomPrefKey = "uiZoom";      // UiPreferences, beside the theme (ADR-68; 02 §5.9)

        // A product flag: on for any value but "" and "0" (CaptureConfig's rule for GPU_LOG).
        bool envFlag(const char* name)
        {
            const char* v = funkgui::env(name);
            return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
        }

        // RenderInfo::renderer: the API EditorHost's frame is drawn with. FunkGui fixes it per platform (BgfxContext,
        // v0.11.0: Metal on Apple, Vulkan everywhere else, never a fallback to another) and has no call that names it.
       #if defined(__APPLE__)
        constexpr const char* kRenderer = "METAL";
       #else
        constexpr const char* kRenderer = "VULKAN";
       #endif
    }

    EditorOptions EditorOptions::fromEnv()
    {
        EditorOptions o;
        o.panel.skipHint = envFlag("UI_NO_HINT");
        o.panel.ignoreLive = envFlag("UI_NO_LIVE");
        // 02 §3.7 rule 7: a fixed-dt capture computes the worker's panes inside tick(), as the probes do, so the frame
        // it dumps cannot depend on when a thread finished. The same parse as EditorHost's own UI_FIXED_DT (> 0 is on).
        o.panel.syncPreview = funkgui::CaptureConfig::fromEnv().fixedDt > 0.0f;
        if (const char* id = funkgui::env("UI_VIEW"); id != nullptr && id[0] != '\0')
        {
            o.view = findView(id);
            if (o.view == nullptr)
                o.unknownView = id;
        }
        return o;
    }

    funkgui::EditorConfig Editor::makeConfig(ProcessorFacade& facade)
    {
        funkgui::EditorConfig config;
        config.width = layout::kWidth;               // 960 × 640, fixed (02 §6.1)
        config.height = layout::kHeight;
        config.fallbackTitle = nullptr;              // FUNKGUI_PRODUCT_NAME upper-cased: "FCOMPRESSOR" (FcmpSources.cmake)
        config.setUiAttached = [&facade](bool on) { facade.setUiAttached(on); };   // the processor outlives the editor
        // UI zoom (ADR-68, ADR-68a; UF1b): the Footer's ZOOM cells offer the same steps (layout::footer::kZoomSteps).
        config.zoomSteps.assign(layout::footer::kZoomSteps.begin(), layout::footer::kZoomSteps.end());
        config.defaultZoomPercent = layout::footer::kDefaultZoomPercent;
        config.zoomPrefKey = kZoomPrefKey;           // machine-wide, like the theme; never saved with the session
        return config;                               // beginBatch/endBatch: empty on purpose (Editor.h)
    }

    std::unique_ptr<Panel> Editor::makePanel(ProcessorFacade& facade, const EditorOptions& options)
    {
        auto panel = std::make_unique<Panel>(facade, options.panel);
        if (options.view != nullptr)
            panel->setView(*options.view, /*instant*/ true);   // before the first tick, as HeadlessHost probes do
        return panel;
    }

    Editor::Editor(juce::AudioProcessor& owner, ProcessorFacade& facade, const EditorOptions& options)
        : funkgui::EditorHost(owner, makeConfig(facade), makePanel(facade, options)),
          ui_(static_cast<Panel&>(funkgui::EditorHost::panel()))    // makePanel made it: the cast is exact
    {
        // ADR-85: the settings screen's DISPLAY row reads the host's own diagnostics (this editor outlives the source:
        // the destructor removes it first).
        ui_.setRenderInfo([this] {
            const funkgui::EditorHost::Diagnostics d = diagnostics();
            RenderInfo r;
            r.gpu = surfaceAttached() && !showingFallback();
            r.displayLinked = d.displayLinked;
            r.fps = d.fps;
            r.scale = d.scale;
            r.zoomPercent = d.zoomPercent;
            r.frames = d.frames;
            r.overflows = d.overflows;
            r.renderer = kRenderer;
            return r;
        });
        if (!options.unknownView.empty())
        {
            juce::String known;
            for (const ViewSpec& v : views())
                known << " " << v.id;
            juce::Logger::writeToLog(juce::String(product::kName) + ": " + product::kEnvPrefix + "UI_VIEW '"
                                     + juce::String::fromUTF8(options.unknownView.c_str()) + "' names no view (known:"
                                     + known + "); opening on the saved view");
        }
    }

    Editor::~Editor()
    {
        // K2 #27: the Panel's worker and gestures go first, while everything they use is alive; ~EditorHost then runs
        // with this class's members already gone and touches only what it owns (02 §5.1 teardown rule).
        ui_.setRenderInfo({});
        ui_.shutdown();
    }
}
