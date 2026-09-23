// Source/editor/SubView.h — the sub-view interface of the fixed Panel composition (02 Part 2 intro; K3 #15), and the
// PanelContext every sub-view and plot is constructed with. Frozen at FZ4.
//
// The Panel (Panel.h) owns nine sub-views in the fixed order of ViewIndex and dispatches to them; Band and CharScreen
// own their plots and dispatch to them the same way (a plot is a SubView of its composite). Nothing here allocates
// per frame. Sub-views never talk to each other directly: what one publishes for another (the item under the hand and
// its spec line, the last touched parameter, the HISTORY freeze column) goes through the PanelContext, and navigation
// (screen, tab, overlay) through Panel::setView.
//
// SubView is 02's struct with its declarations unchanged. Five virtuals are added, each with a default so a stub needs
// none of them: 02 Part 2 says the Panel's pointer*, cursor, a11yRevision and a11yAction overrides "each dispatch to
// the sub-views", but the struct listing has no member for them to dispatch to (U1a handoff).
#pragma once

#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"
#include "plugin/ProcessorFacade.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace funkgui
{
    class Canvas;
    class FontAtlasSdf;
    class GestureController;
    class HostServices;
    struct Theme;
}

namespace fcdsp
{
    struct ModeEntry;
}

namespace fcmp::ui
{
    class HistoryStore;
    class Panel;
    class PreviewWorker;
    class SlotModel;
    struct PanelOptions;
    enum class Screen  : uint8_t;                    // Panel.h
    enum class Overlay : uint8_t;                    // Panel.h

    struct SubView                                   // one region of the fixed panel; no allocation per frame
    {
        virtual ~SubView() = default;
        virtual void tick(float dt) = 0;
        virtual void draw(funkgui::Canvas&, const funkgui::Theme&) const = 0;
        virtual bool hit(funkgui::Point) const = 0;
        virtual void pointerDown(const funkgui::PointerEvent&) {}
        virtual void pointerDrag(const funkgui::PointerEvent&) {}
        virtual void pointerUp(const funkgui::PointerEvent&) {}
        virtual void doubleClick(const funkgui::PointerEvent&) {}
        virtual bool wheel(const funkgui::WheelEvent&) { return false; }
        virtual bool key(const funkgui::KeyEvent&) { return false; }
        virtual void accessibility(std::vector<funkgui::A11yItem>&) const = 0;   // ids = (subViewIndex << 16) | local
        virtual int  focusOrder(std::span<uint32_t> out) const = 0;             // this sub-view's Tab stops, in order
        virtual bool wantsFullRate() const { return false; }

        // ---- the Panel's dispatch targets 02's struct omitted (defaults: no-ops) ---------------------------------------
        virtual void pointerMove(const funkgui::PointerEvent&) {}    // the pointer is over this sub-view (hover)
        virtual void pointerExit() {}                                // it left this sub-view (or the window)
        virtual funkgui::Cursor cursor(funkgui::Point) const { return funkgui::Cursor::normal; }
        virtual void a11yAction(uint32_t /*id*/, funkgui::A11yAction, double /*value*/) {}   // an id of this sub-view
        virtual uint32_t a11yRevision() const { return 0; }          // bumps when its items appear, disappear or move
    };

    // ---- a11y ids and the fixed composition -----------------------------------------------------------------------------

    // The nine sub-views, in the composition order of 02 Part 2 (the Panel's views_ index; the high half of every a11y
    // id). Draw, hit and Tab orders are fixed tables in Panel.cpp.
    enum class ViewIndex : uint8_t
    {
        header, displayRow, slotGrid, band, charScreen, modeBrowser, presetStrip, presetBrowser, footer
    };
    inline constexpr int kSubViewCount = 9;

    // id = (subViewIndex << 16) | local, local in 1..65535 (0 is "no item" everywhere).
    constexpr uint32_t a11yId(ViewIndex v, uint32_t local) noexcept
    {
        return (static_cast<uint32_t>(v) << 16) | (local & 0xFFFFu);
    }
    constexpr int viewIndexOf(uint32_t id) noexcept { return static_cast<int>(id >> 16); }

    // A composite (Band, CharScreen) gives its plot k the local ids [(k + 1)·256, (k + 2)·256); its own items use
    // 1..255. Inside a plot, locals 1..127 are its cells, groups, buttons and image, and 128..255 its handles and markers,
    // so a composite can put every plot's chrome stops before every plot's handle stops (02 §7.5 Tab order).
    inline constexpr uint32_t kPlotIdStride  = 256;
    inline constexpr uint32_t kPlotHandleIds = 128;
    constexpr uint32_t plotIdBase(ViewIndex v, int plot) noexcept
    {
        return a11yId(v, static_cast<uint32_t>(plot + 1) * kPlotIdStride);
    }
    constexpr int plotIndexOf(uint32_t id) noexcept
    {
        return static_cast<int>((id & 0xFFFFu) / kPlotIdStride) - 1;     // -1: the composite's own item
    }
    constexpr bool isHandleId(uint32_t id) noexcept { return (id & (kPlotIdStride - 1)) >= kPlotHandleIds; }

    // ---- per-frame state ------------------------------------------------------------------------------------------------

    // What every view reads this frame (02 §9.1–§9.4), filled by Panel::tick before any sub-view ticks: the raw values
    // resolved once (cached by the raw-snapshot hash), the latest UiFrame and its staleness, and the EngineParams the
    // curves are drawn from — resolve() with, while live, the smoothed fields overlaid (overlaySmoothed; K1 #7, K2 #24).
    struct FrameState
    {
        fcdsp::RawParams        raw{};              // ProcessorFacade::currentRaw() this frame (budget included)
        const fcdsp::ModeEntry* entry = nullptr;    // resolveSlot(raw.modeSlot).entry; nullptr: no Mode registered
        fcdsp::Resolution       res{};              // resolve(*entry, raw): the view and the pure EngineParams
        fcdsp::EngineParams     eng{};              // res.eng, with overlaySmoothed(ui) while `overlaid`
        fcdsp::UiFrame          ui{};               // the last UiFrame read (kept when a read fails)
        bool     hasFrame = false;                  // a UiFrame has been read
        bool     fresh    = false;                  // publishCount moved within layout::band::kStaleS (never with
                                                    // PanelOptions::ignoreLive: drawn as if stale)
        bool     live     = false;                  // fresh and kUiLive set
        bool     overlaid = false;                  // eng carries the live smoothed fields
        float    staleSeconds = 0.0f;               // since publishCount last moved
        uint64_t rawHash = 0;                       // FNV-1a of raw (values, slot, budget): SlotModel keys
        uint64_t engHash = 0;                       // FNV-1a of eng: curve caches
        uint32_t resolveSerial = 0;                 // bumps when res is recomputed
        uint32_t engSerial = 0;                     // bumps when engHash changes
        uint32_t modeSerial = 0;                    // bumps when the resolved Mode slot changes (landing, 02 §8.7)
        uint8_t  prevSlot = 0;                      // the slot before the last Mode change
    };

    // The item under the hand (02 §6.5 accents, §6.6 display precedence and footer). Sub-views offer theirs during tick
    // (PanelContext::offerHand); the strongest offer of the frame wins (drag > hover > focus).
    enum class HandKind : uint8_t { none, focus, hover, drag };
    struct HandState
    {
        fcdsp::Pid pid  = fcdsp::kNoPid;            // the parameter it controls (kNoPid: none, or not a parameter)
        HandKind   kind = HandKind::none;
        uint32_t   item = 0;                        // its a11y id
        char       spec[256] {};                    // its footer spec line (UTF-8, "" = none)
    };

    // HISTORY press-and-hold (02 §6.5): while active the display row shows this column
    // ("AT −1.24 S · GR −3.8 · IN −8.1 · OUT −11.9").
    struct FreezeState
    {
        bool                 active = false;
        float                atSeconds = 0.0f;      // <= 0: seconds before now
        fcdsp::HistoryColumn column{};
    };

    // Everything a sub-view is constructed with. Owned by the Panel, which outlives every sub-view; services are fixed
    // for the Panel's life, the rest is rewritten every frame or input event. Message thread only.
    struct PanelContext
    {
        PanelContext(Panel& p, ProcessorFacade& f, const PanelOptions& o, const funkgui::FontAtlasSdf& a,
                     HistoryStore& h, PreviewWorker& w) noexcept
            : panel(p), facade(f), options(o), atlas(a), history(h), preview(w) {}
        PanelContext(const PanelContext&) = delete;
        PanelContext& operator=(const PanelContext&) = delete;

        // ---- services ---------------------------------------------------------------------------------------------------
        Panel&                        panel;        // navigation: Panel::setView (the CHARACTERISTICS latch, browsers)
        ProcessorFacade&              facade;       // ports, telemetry, UiState, notices, presets
        const PanelOptions&           options;
        const funkgui::FontAtlasSdf&  atlas;        // FontService's atlas: text measurement (text::width, fits)
        HistoryStore&                 history;      // drained by the Panel every frame
        PreviewWorker&                preview;      // step responses; active only on CHARACTERISTICS
        std::array<SlotModel*, fcdsp::kNumModeParams> slots{};   // one per Mode-filtered Pid, shared by slots and handles
        funkgui::HostServices*        host = nullptr;       // after Panel::attach: the host menu, unbounded drags
        funkgui::GestureController*   gestures = nullptr;   // after Panel::attach: every parameter write goes here

        // ---- navigation (read-only here; Panel::setView changes it) --------------------------------------------------
        Screen   screen{};
        ScTab    scTab = ScTab::sidechain;
        Overlay  overlay{};

        // ---- this frame -------------------------------------------------------------------------------------------------
        FrameState frame;
        double     seconds = 0.0;                   // panel time: the sum of every tick's dt (never a wall clock)

        // ---- pointer and keyboard -----------------------------------------------------------------------------------
        funkgui::Point pointer{};                   // the last pointer position, logical px
        bool     pointerIn = false;                 // inside the window
        bool     pointerPressed = false;            // between pointerDown and pointerUp
        uint32_t pointerMoves = 0;                  // counts of pointerMove / pointerDown events (the first-run hint)
        uint32_t pointerDowns = 0;
        bool     alwaysChrome = false;              // a pointer down with no move ever seen: touch mode (HR :1488)
        uint32_t focus = 0;                         // the a11y id with keyboard focus (0: none)
        bool     focusVisible = false;              // the focus ring is shown (Tab shows it; a click or Esc hides it)

        // ---- cross-view channels ------------------------------------------------------------------------------------
        HandState  hand;                            // the item under the hand as of the last completed tick (read)
        HandState  handNext;                        // this tick's offers (write through offerHand)
        fcdsp::Pid touched = fcdsp::kNoPid;         // the last parameter written by wheel, key or a11y ...
        double     touchedAt = -1.0;                // ... at this panel time (display dwell, layout::display::kDwellS)
        FreezeState freeze;

        // ---- machine-wide preferences mirrored for tick/draw (02 §5.9; no file access in tick or draw, §3.7) ----------
        int meterScaleDb = 48;                      // S of the shared level map, layout::kScalesDb
        int historySpanTenths = 50;                 // HISTORY span in tenths of a second, layout::kSpansTenths

        SlotModel& slot(fcdsp::Pid p) const noexcept                // Mode-filtered Pids only (idx < 22)
        {
            assert(fcdsp::idx(p) < fcdsp::kNumModeParams && slots[fcdsp::idx(p)] != nullptr);
            return *slots[fcdsp::idx(p)];
        }

        // Offer the item under the hand for this frame; the strongest kind wins, the first offer wins a tie.
        void offerHand(fcdsp::Pid pid, HandKind kind, uint32_t item, const char* spec) noexcept
        {
            if (kind == HandKind::none || kind <= handNext.kind)
                return;
            handNext.pid = pid;
            handNext.kind = kind;
            handNext.item = item;
            std::size_t n = 0;
            if (spec != nullptr)
                while (spec[n] != '\0' && n + 1 < sizeof handNext.spec)
                {
                    handNext.spec[n] = spec[n];
                    ++n;
                }
            while (n > 0 && spec != nullptr && spec[n] != '\0'                    // cut: never inside a UTF-8 sequence
                   && (static_cast<unsigned char>(spec[n]) & 0xC0u) == 0x80u)
                --n;
            handNext.spec[n] = '\0';
        }

        // A discrete write (wheel, key, a11y) to `pid`: the display row shows it for layout::display::kDwellS.
        void touch(fcdsp::Pid pid) noexcept
        {
            touched = pid;
            touchedAt = seconds;
        }
    };
}
