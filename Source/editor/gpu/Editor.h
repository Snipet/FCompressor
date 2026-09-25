// Source/editor/gpu/Editor.h — the FCompressor GPU editor (02 Part 2 intro, §5.1, §6.1; U7). GPU configuration only:
// Source/plugin/CreateEditorGpu.cpp constructs it, and cmake/FcmpSources.cmake compiles Source/editor/gpu/ into the GPU
// plugin alone (never into fcmp_probe_plugin, which runs the same Panel headless).
//
// fcmp::ui::Editor is a funkgui::EditorHost (FunkGui v0.8.0) running one fcmp::ui::Panel:
// - Size: 960 × 640 logical px (layout::kWidth/kHeight) times the UI zoom (ADR-68, which revises ADR-06's "one setSize";
//   ADR-68a; UF1b). EditorConfig carries the zoom: steps 100 / 125 / 150 / 175 % (layout::footer::kZoomSteps, the
//   Footer's ZOOM cells), default 125 %, the machine-wide UiPreferences int "uiZoom" beside the theme. EditorHost does
//   the whole zoom — the window (setSize on a change; the host follows), the render density, input and accessibility
//   coordinates, every open editor following the preference, and a step whose window exceeds the display's user area
//   drawn at the largest step that fits — while the Panel draws, hit-tests and lists accessibility in its logical px.
//   The window is not user-resizable (setResizable(false, false)); nothing here calls setSize (02 §6.1; K1 #34).
//   Captures: FCMP_UI_ZOOM=<percent> pins the zoom; under FCMP_CANVAS_DUMP it is 100 % unless FCMP_UI_ZOOM is set.
// - Ownership: EditorHost owns the Panel (its unique_ptr); this class keeps a typed reference for the teardown. The
//   parameter ports belong to the processor (ProcessorFacade::port), so they outlive every editor (K2 #27).
// - Teardown (02 §5.1 teardown rule; K2 #27): ~Editor() calls Panel::shutdown() — the PreviewWorker stops first, then
//   the open gestures close — before ~EditorHost runs (which closes gestures again, a no-op by then, then drops the
//   telemetry attach, leaves the FramePump and detaches the surface).
// - Placement: FunkGui's EditorHost (since v0.7.1) keeps its render view on the editor when an ANCESTOR moves inside the
//   same window (JUCE's Standalone lays its content out under the title bar after the view attaches), so U7's
//   product-side AncestorWatcher is gone (UF1a, S11).
// - Telemetry: EditorConfig::setUiAttached is ProcessorFacade::setUiAttached, so the processor publishes UiFrames and
//   history columns exactly while an editor lives (count-based: two open editors keep it on until both close).
// - Batches: EditorConfig::beginBatch/endBatch stay empty. The Panel's own HostServices proxy already brackets every
//   GestureController batch with ProcessorFacade::beginBatch/endBatch (Panel.h), and a second bracket would only nest.
// - Product hooks (02 §5.1, §3.7), read once through funkgui::env() into EditorOptions when the editor opens:
//     FCMP_UI_VIEW=<ViewSpec::id>   the view to open on (panel, chars.sidechain, chars.colour, modebrowser,
//                                   presetbrowser), set instantly; unset or unknown: the view the instance's UiState
//                                   restores (an unknown id is logged once)
//     FCMP_UI_NO_HINT=1             PanelOptions::skipHint (no first-run hint)
//     FCMP_UI_NO_LIVE=1             PanelOptions::ignoreLive (draw as if telemetry were stale; parity captures)
//     FCMP_UI_FIXED_DT=<sec>        also PanelOptions::syncPreview: worker-computed panes are finished inside tick(), so
//                                   no drawn state depends on another thread's completion time (02 §3.7 rule 7)
//   A flag is on for any value but "" and "0" (CaptureConfig's rule for FCMP_GPU_LOG). The capture and diagnostics
//   variables (FCMP_CANVAS_DUMP, _UI_THEME, _UI_SCALE, _UI_ZOOM, _UI_KEYS, _A11Y_DUMP, _GPU_LOG, ...) are EditorHost's
//   own.
//
// Message thread only.
#pragma once

#include "editor/Panel.h"
#include "plugin/ProcessorFacade.h"

#include <funkgui/gpu/EditorHost.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>

namespace fcmp::ui
{
    // The product-level hooks of one editor, from the FCMP_ environment (see the top of this file).
    struct EditorOptions
    {
        PanelOptions    panel{};
        const ViewSpec* view = nullptr;              // FCMP_UI_VIEW; nullptr: the view UiState restores
        std::string     unknownView;                 // FCMP_UI_VIEW named no view (logged by the editor)

        static EditorOptions fromEnv();
    };

    class Editor final : public funkgui::EditorHost
    {
    public:
        // `owner` and `facade` are the same object (fcmp::Processor implements both); the editor reaches the
        // processor only through the facade (01 §2.2, lint editor.facade).
        Editor(juce::AudioProcessor& owner, ProcessorFacade& facade, const EditorOptions& options);
        ~Editor() override;                          // Panel::shutdown(): the worker stops, then the gestures close

        Editor(const Editor&) = delete;
        Editor& operator=(const Editor&) = delete;

        Panel&       ui() noexcept { return ui_; }   // the Panel EditorHost owns, typed
        const Panel& ui() const noexcept { return ui_; }

    private:
        static funkgui::EditorConfig  makeConfig(ProcessorFacade&);
        static std::unique_ptr<Panel> makePanel(ProcessorFacade&, const EditorOptions&);

        Panel& ui_;                                  // == EditorHost::panel(); valid for the editor's whole life

        JUCE_LEAK_DETECTOR(Editor)
    };
}
