// Source/editor/Panel.h — the FCompressor panel (02 Part 2 intro; K1 #10, K3 #15): a GPU-free funkgui::Panel that is a
// FIXED COMPOSITION of nine sub-views (SubView.h) and owns no drawing of its own beyond dispatch. EditorHost runs it
// live (gpu/Editor, U7); probes run it headless through funkgui::HeadlessHost over a FakeFacade. Frozen at FZ4.
//
// The composition (Panel.cpp):
// - Sub-views, in ViewIndex order: Header, DisplayRow, SlotGrid, Band, CharScreen, ModeBrowser, PresetStrip,
//   PresetBrowser, Footer. The chrome (Header, PresetStrip, DisplayRow, Footer) is live on both screens; SlotGrid and
//   Band only on PANEL, CharScreen only on CHARACTERISTICS (Q4: only the middle region y 124–600 swaps); a browser
//   only while its overlay is open.
// - Draw order: Header, PresetStrip, DisplayRow, the middle region (during a screen change the outgoing screen at
//   1 − a, then the incoming one at a: a theme whose every token is faded, so rects and text draw at that alpha and
//   premixed curves premix toward the ground, 02 §7.1), Footer, then the open browser (faded in, τ 0.12 s).
// - Hit order: the open browser, PresetStrip, Header, DisplayRow, Footer, SlotGrid, Band, CharScreen. A pointer down
//   is captured by the sub-view it hit until the pointer is released, even across a screen change (it still gets
//   pointerUp and ends its gesture). A pointer down outside an open browser closes it and is consumed (02 §8.6
//   "clicking outside cancels").
// - Tab order: Header, PresetStrip, DisplayRow, Band, SlotGrid, CharScreen, Footer — each live sub-view's
//   focusOrder() in turn (02 §8.9, §7.5). Keys go to the open browser first, then to the focused item's sub-view while
//   the focus ring is shown. Esc: close the browser, else hide the ring, else leave CHARACTERISTICS.
// - Every frame, before the sub-views tick: the raw values are resolved (cached by their hash), the UiFrame is read
//   (staleness 0.5 s), the live smoothed fields are overlaid, the HistoryRing is drained into the HistoryStore, and the
//   PreviewWorker runs (inline with PanelOptions::syncPreview). The screen crossfade eases with τ 0.12 s and snaps at
//   1e−3 (ScreenFader's contract, 02 §5.7, until G6's DwellSelector lands).
// - HostServices::beginBatch/endBatch from GestureController::tapMany reach ProcessorFacade::beginBatch/endBatch (K2 #23).
// - Teardown (K2 #27): shutdown() stops the PreviewWorker, then closes the open gestures; the destructor calls it
//   when the owner has not. Parameter ports belong to the facade and outlive the Panel.
#pragma once

#include "editor/SubView.h"
#include "plugin/ProcessorFacade.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>
#include <funkgui/panel/Panel.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace funkgui
{
    class Canvas;
    class GestureController;
    class HostServices;
    struct Theme;
}

namespace fcmp::ui
{
    class HistoryStore;
    class PreviewWorker;
    class SlotModel;

    enum class Screen  : uint8_t { panel, characteristics };
    enum class Overlay : uint8_t { none, modeBrowser, presetBrowser };
    struct ViewSpec { const char* id; Screen screen; ScTab tab; Overlay overlay; };   // ScTab: ProcessorFacade.h

    // "panel", "chars.sidechain", "chars.colour", "modebrowser", "presetbrowser" — the G1 golden views, in this order.
    std::span<const ViewSpec> views() noexcept;

    // The view whose id is `id` (FCMP_UI_VIEW, `ui.dump --view`); nullptr for an unknown id. (Addition to 02.)
    const ViewSpec* findView(std::string_view id) noexcept;

    struct PanelOptions
    {
        bool skipHint    = false;   // probes; FCMP_UI_NO_HINT=1
        bool syncPreview = false;   // step responses computed inside tick() (determinism rule 7, §3.7)
        bool ignoreLive  = false;   // draw as if telemetry were stale; FCMP_UI_NO_LIVE=1
    };

    class Panel final : public funkgui::Panel
    {
    public:
        Panel(ProcessorFacade&, PanelOptions);
        ~Panel() override;                                       // shutdown() if not yet done

        Panel(const Panel&) = delete;
        Panel& operator=(const Panel&) = delete;

        void setView(const ViewSpec&, bool instant = true);      // probes and FCMP_UI_VIEW; users use the latch/browsers
        void shutdown();                                         // stop PreviewWorker, then closeGestures() (§5.1)

        // ---- additions: read-only state for sub-views, the GPU editor and probes ----------------------------------------
        Screen  screen() const noexcept;
        Overlay overlay() const noexcept;
        ScTab   scTab() const noexcept;
        bool    isShutDown() const noexcept;
        const PanelContext& context() const noexcept;            // this frame's state, the hand, focus, services

        // ---- funkgui::Panel: each dispatches to the sub-views in the fixed orders above --------------------------------
        void  attach(funkgui::HostServices&) override;
        int   width() const override;                            // 960
        int   height() const override;                           // 640
        void  tick(float dt) override;
        void  idle(double nowSec) override;                      // closes idle wheel gestures
        void  draw(funkgui::Canvas&, const funkgui::Theme&) override;
        bool  wantsFullRate() const override;
        void  pointerMove(const funkgui::PointerEvent&) override;
        void  pointerExit() override;
        void  pointerDown(const funkgui::PointerEvent&) override;
        void  pointerDrag(const funkgui::PointerEvent&) override;
        void  pointerUp(const funkgui::PointerEvent&) override;
        void  doubleClick(const funkgui::PointerEvent&) override;
        bool  wheel(const funkgui::WheelEvent&) override;
        bool  key(const funkgui::KeyEvent&) override;
        funkgui::Cursor cursor() const override;
        void  accessibility(std::vector<funkgui::A11yItem>&) const override;   // hidden sub-views' items: visible=false
        uint32_t a11yRevision() const override;
        void  a11yAction(uint32_t id, funkgui::A11yAction, double value = 0) override;
        void  closeGestures() override;

        // ---- U6 addition (S12 lead revision 8): a preset file dropped on the panel imports (PresetBrowser) -------------
        bool  filesInterest(const std::vector<std::string>&) const override;
        void  filesDropped(const std::vector<std::string>&) override;

    private:
        class HostProxy;                                         // HostServices over the host + the facade's batch

        bool live(ViewIndex) const noexcept;                     // takes input this frame
        bool shown(ViewIndex) const noexcept;                    // drawn and ticked this frame (live or fading out)
        SubView& view(ViewIndex) const noexcept;
        int  hitView(funkgui::Point) const noexcept;             // ViewIndex under p in hit order, -1: none
        void refreshFrame(float dt);
        void drawScreen(funkgui::Canvas&, Screen, const funkgui::Theme&) const;
        int  composeFocusOrder(std::span<uint32_t>) const;
        bool moveFocus(int dir);                                 // false: there is no Tab stop
        void closeOverlay();

        ProcessorFacade&                  facade_;
        const PanelOptions                options_;
        std::unique_ptr<HistoryStore>     history_;
        std::unique_ptr<PreviewWorker>    preview_;
        PanelContext                      ctx_;
        std::array<std::unique_ptr<SlotModel>, fcdsp::kNumModeParams> slots_;
        std::unique_ptr<HostProxy>        proxy_;
        std::unique_ptr<funkgui::GestureController> gestures_;

        Screen   screen_   = Screen::panel;
        Screen   outgoing_ = Screen::panel;                      // the screen fading out while fade_ < 1
        float    fade_     = 1.0f;                               // ScreenFader amount: the incoming screen's alpha
        ScTab    scTab_    = ScTab::sidechain;
        Overlay  overlay_  = Overlay::none;                      // the open browser (input goes here)
        Overlay  drawnOverlay_ = Overlay::none;                  // the browser drawn while overlayAmt_ > 0
        float    overlayAmt_ = 0.0f;
        int      captured_ = -1;                                 // ViewIndex holding the pointer between down and up
        int      hovered_  = -1;                                 // ViewIndex under the pointer
        uint8_t  lastSlot_ = 0;
        bool     resolvedOnce_ = false;
        bool     ticked_   = false;
        bool     shutDown_ = false;
        uint32_t a11yRevision_ = 1;

        std::array<std::unique_ptr<SubView>, kSubViewCount> views_;   // last: destroyed first (they reference the above)
    };
}
