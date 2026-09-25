// Source/editor/Panel.cpp — the fixed composition (see Panel.h for the orders and the per-frame pipeline).
#include "editor/Panel.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/PreviewWorker.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"
#include "editor/views/Band.h"
#include "editor/views/CharScreen.h"
#include "editor/views/DisplayRow.h"
#include "editor/views/Footer.h"
#include "editor/views/Header.h"
#include "editor/views/ModeBrowser.h"
#include "editor/views/PresetBrowser.h"
#include "editor/views/PresetStrip.h"
#include "editor/views/SlotGrid.h"

#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/text/FontService.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <span>

namespace fcmp::ui
{
    namespace
    {
        constexpr std::array<ViewSpec, 5> kViews { {
            { "panel",           Screen::panel,           ScTab::sidechain, Overlay::none },
            { "chars.sidechain", Screen::characteristics, ScTab::sidechain, Overlay::none },
            { "chars.colour",    Screen::characteristics, ScTab::colour,    Overlay::none },
            { "modebrowser",     Screen::panel,           ScTab::sidechain, Overlay::modeBrowser },
            { "presetbrowser",   Screen::panel,           ScTab::sidechain, Overlay::presetBrowser },
        } };

        // The fixed orders of Panel.h.
        constexpr std::array<ViewIndex, 9> kHitOrder { ViewIndex::modeBrowser, ViewIndex::presetBrowser,
                                                       ViewIndex::presetStrip, ViewIndex::header,
                                                       ViewIndex::displayRow, ViewIndex::footer,
                                                       ViewIndex::slotGrid, ViewIndex::band, ViewIndex::charScreen };
        constexpr std::array<ViewIndex, 7> kTabOrder { ViewIndex::header, ViewIndex::presetStrip, ViewIndex::displayRow,
                                                       ViewIndex::band, ViewIndex::slotGrid, ViewIndex::charScreen,
                                                       ViewIndex::footer };
        constexpr std::size_t kMaxTabStops = 1024;

        constexpr std::size_t at(ViewIndex v) noexcept { return static_cast<std::size_t>(v); }

        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime  = 1099511628211ull;

        uint64_t fnv(const void* p, std::size_t n, uint64_t h = kFnvOffset) noexcept
        {
            const auto* b = static_cast<const unsigned char*>(p);
            for (std::size_t i = 0; i < n; ++i)
            {
                h ^= b[i];
                h *= kFnvPrime;
            }
            return h;
        }

        uint64_t hashRaw(const fcdsp::RawParams& r) noexcept
        {
            uint64_t h = fnv(r.v.data(), sizeof(float) * r.v.size());
            h = fnv(&r.modeSlot, sizeof r.modeSlot, h);
            const auto budget = static_cast<uint8_t>(r.budget);
            return fnv(&budget, sizeof budget, h);
        }

        uint64_t hashEng(const fcdsp::EngineParams& e) noexcept { return fnv(&e, sizeof e); }   // no padding (116 B)

        // A theme whose every token is faded to alpha a: rects and text of the outgoing or incoming layer draw at that
        // alpha, and curves drawn with premix(ground, token, …) premix toward the ground (a colour's alpha counts as
        // coverage in premix), which is 02 §7.1's crossfade.
        funkgui::Theme faded(const funkgui::Theme& t, float a) noexcept
        {
            funkgui::Theme f = t;
            f.ground = funkgui::fade(t.ground, a);
            f.ink100 = funkgui::fade(t.ink100, a);
            f.ink70 = funkgui::fade(t.ink70, a);
            f.ink52 = funkgui::fade(t.ink52, a);
            f.ink32 = funkgui::fade(t.ink32, a);
            f.ink16 = funkgui::fade(t.ink16, a);
            f.accent = funkgui::fade(t.accent, a);
            f.accentDim = funkgui::fade(t.accentDim, a);
            f.signal = funkgui::fade(t.signal, a);
            return f;
        }

        ViewIndex overlayView(Overlay o) noexcept
        {
            return o == Overlay::presetBrowser ? ViewIndex::presetBrowser : ViewIndex::modeBrowser;
        }
    }

    std::span<const ViewSpec> views() noexcept { return kViews; }

    const ViewSpec* findView(std::string_view id) noexcept
    {
        for (const ViewSpec& v : kViews)
            if (id == v.id)
                return &v;
        return nullptr;
    }

    // ---- HostServices proxy: batches reach the facade (K2 #23) ----------------------------------------------------------

    // Everything else is the host's: FunkGui v0.7.1's themeIndex() (the Theme the next draw receives, valid at once after
    // a THEME click) and ownerComponent() (the juce::Component a PopupMenu or FileChooser anchors to; nullptr headless)
    // are forwarded, so a sub-view asking PanelContext::host gets the host's answer, never the defaults. So is v0.8.0's UI
    // zoom (UF1b; ADR-68, ADR-68a), for the Footer's ZOOM cells: zoomPercent(), setZoomPercent(), zoomSteps() and
    // zoomFits(). Without them the Panel would always see 100 %, no steps and every step fitting.
    class Panel::HostProxy final : public funkgui::HostServices
    {
    public:
        HostProxy(funkgui::HostServices& host, ProcessorFacade& facade) noexcept : host_(host), facade_(facade) {}

        void   setUnboundedDrag(bool on) override { host_.setUnboundedDrag(on); }
        void   showParamMenu(funkgui::ParamPort& p, float x, float y) override { host_.showParamMenu(p, x, y); }
        void   nudgeFullRate() override { host_.nudgeFullRate(); }
        double nowSeconds() const override { return host_.nowSeconds(); }
        int    themeIndex() const override { return host_.themeIndex(); }
        juce::Component* ownerComponent() override { return host_.ownerComponent(); }
        int    zoomPercent() const override { return host_.zoomPercent(); }
        void   setZoomPercent(int percent) override { host_.setZoomPercent(percent); }
        std::span<const int> zoomSteps() const override { return host_.zoomSteps(); }
        bool   zoomFits(int percent) const override { return host_.zoomFits(percent); }
        void   beginBatch() override
        {
            facade_.beginBatch();
            host_.beginBatch();
        }
        void   endBatch() override
        {
            host_.endBatch();
            facade_.endBatch();                                  // the outermost end raises the engine snap
        }

    private:
        funkgui::HostServices& host_;
        ProcessorFacade&       facade_;
    };

    // ---- construction and teardown ------------------------------------------------------------------------------------

    Panel::Panel(ProcessorFacade& facade, PanelOptions options)
        : facade_(facade),
          options_(options),
          history_(std::make_unique<HistoryStore>()),
          preview_(std::make_unique<PreviewWorker>(options.syncPreview)),
          ctx_(*this, facade, options_, funkgui::FontService::get().atlas(), *history_, *preview_)
    {
        tag::registerTagNames();
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
        {
            slots_[i] = std::make_unique<SlotModel>(facade_, ctx_.frame, static_cast<fcdsp::Pid>(i));
            ctx_.slots[i] = slots_[i].get();
        }

        // The per-instance UI state restores the screen and the tab (01 §9.1); a session opens without a browser.
        const UiState& st = facade_.uiState();
        screen_ = outgoing_ = st.charExpanded ? Screen::characteristics : Screen::panel;
        scTab_ = st.scTab;
        ctx_.screen = screen_;
        ctx_.scTab = scTab_;
        ctx_.overlay = overlay_;

        refreshFrame(0.0f);                                      // the sub-views see a resolved frame from the start

        views_[at(ViewIndex::header)]        = std::make_unique<Header>(ctx_);
        views_[at(ViewIndex::displayRow)]    = std::make_unique<DisplayRow>(ctx_);
        views_[at(ViewIndex::slotGrid)]      = std::make_unique<SlotGrid>(ctx_);
        views_[at(ViewIndex::band)]          = std::make_unique<Band>(ctx_);
        views_[at(ViewIndex::charScreen)]    = std::make_unique<CharScreen>(ctx_);
        views_[at(ViewIndex::modeBrowser)]   = std::make_unique<ModeBrowser>(ctx_);
        views_[at(ViewIndex::presetStrip)]   = std::make_unique<PresetStrip>(ctx_);
        views_[at(ViewIndex::presetBrowser)] = std::make_unique<PresetBrowser>(ctx_);
        views_[at(ViewIndex::footer)]        = std::make_unique<Footer>(ctx_);

        preview_->setActive(screen_ == Screen::characteristics);
    }

    Panel::~Panel() { shutdown(); }

    void Panel::shutdown()
    {
        if (shutDown_)
            return;
        preview_->stop();                                        // K2 #27: the worker first ...
        closeGestures();                                         // ... then the open gestures
        shutDown_ = true;
    }

    // ---- navigation ---------------------------------------------------------------------------------------------------

    void Panel::setView(const ViewSpec& v, bool instant)
    {
        if (v.screen != screen_)
        {
            // Reversing a running crossfade continues from where it is instead of jumping.
            const bool reversing = fade_ < 1.0f && v.screen == outgoing_;
            outgoing_ = screen_;
            screen_ = v.screen;
            fade_ = instant ? 1.0f : (reversing ? 1.0f - fade_ : 0.0f);
            facade_.uiState().charExpanded = screen_ == Screen::characteristics;
            preview_->setActive(screen_ == Screen::characteristics);
            ++a11yRevision_;
        }
        else if (instant)
        {
            fade_ = 1.0f;
        }
        if (v.tab != scTab_)
        {
            scTab_ = v.tab;
            facade_.uiState().scTab = scTab_;
            ++a11yRevision_;
        }
        if (v.overlay != overlay_)
        {
            overlay_ = v.overlay;
            if (overlay_ != Overlay::none)
                drawnOverlay_ = overlay_;
            ++a11yRevision_;
        }
        if (instant)
        {
            overlayAmt_ = overlay_ != Overlay::none ? 1.0f : 0.0f;
            if (overlay_ == Overlay::none)
                drawnOverlay_ = Overlay::none;
        }
        // A capture survives (the captured sub-view still gets its pointerUp and ends its gesture); a hover on a
        // sub-view that is no longer live ends here.
        if (hovered_ >= 0 && !live(static_cast<ViewIndex>(hovered_)))
        {
            views_[static_cast<std::size_t>(hovered_)]->pointerExit();
            hovered_ = -1;
        }
        ctx_.screen = screen_;
        ctx_.scTab = scTab_;
        ctx_.overlay = overlay_;
    }

    void Panel::closeOverlay() { setView({ nullptr, screen_, scTab_, Overlay::none }, false); }

    Screen  Panel::screen() const noexcept { return screen_; }
    Overlay Panel::overlay() const noexcept { return overlay_; }
    ScTab   Panel::scTab() const noexcept { return scTab_; }
    bool    Panel::isShutDown() const noexcept { return shutDown_; }
    const PanelContext& Panel::context() const noexcept { return ctx_; }

    SubView& Panel::view(ViewIndex v) const noexcept { return *views_[at(v)]; }

    bool Panel::live(ViewIndex v) const noexcept
    {
        switch (v)
        {
            case ViewIndex::header:
            case ViewIndex::displayRow:
            case ViewIndex::presetStrip:
            case ViewIndex::footer:        return true;
            case ViewIndex::slotGrid:
            case ViewIndex::band:          return screen_ == Screen::panel;
            case ViewIndex::charScreen:    return screen_ == Screen::characteristics;
            case ViewIndex::modeBrowser:   return overlay_ == Overlay::modeBrowser;
            case ViewIndex::presetBrowser: return overlay_ == Overlay::presetBrowser;
        }
        return false;
    }

    bool Panel::shown(ViewIndex v) const noexcept
    {
        const bool fading = fade_ < 1.0f;
        switch (v)
        {
            case ViewIndex::header:
            case ViewIndex::displayRow:
            case ViewIndex::presetStrip:
            case ViewIndex::footer:        return true;
            case ViewIndex::slotGrid:
            case ViewIndex::band:          return screen_ == Screen::panel || (fading && outgoing_ == Screen::panel);
            case ViewIndex::charScreen:    return screen_ == Screen::characteristics
                                               || (fading && outgoing_ == Screen::characteristics);
            case ViewIndex::modeBrowser:   return overlayAmt_ > 0.0f && drawnOverlay_ == Overlay::modeBrowser;
            case ViewIndex::presetBrowser: return overlayAmt_ > 0.0f && drawnOverlay_ == Overlay::presetBrowser;
        }
        return false;
    }

    int Panel::hitView(funkgui::Point p) const noexcept
    {
        for (const ViewIndex v : kHitOrder)
            if (live(v) && view(v).hit(p))
                return static_cast<int>(v);
        return -1;
    }

    // ---- funkgui::Panel -------------------------------------------------------------------------------------------------

    void Panel::attach(funkgui::HostServices& host)
    {
        if (gestures_)
            gestures_->closeAll();                               // a second host: nothing stays open on the first
        proxy_ = std::make_unique<HostProxy>(host, facade_);
        gestures_ = std::make_unique<funkgui::GestureController>(*proxy_);
        ctx_.host = proxy_.get();
        ctx_.gestures = gestures_.get();
    }

    int Panel::width() const { return layout::kWidth; }
    int Panel::height() const { return layout::kHeight; }

    void Panel::refreshFrame(float dt)
    {
        FrameState& f = ctx_.frame;
        f.raw = facade_.currentRaw();
        const uint64_t rawHash = hashRaw(f.raw);
        const fcdsp::ModeEntry* entry = fcdsp::resolveSlot(f.raw.modeSlot).entry;
        if (!resolvedOnce_ || rawHash != f.rawHash || entry != f.entry)
        {
            f.entry = entry;
            f.rawHash = rawHash;
            if (entry != nullptr)
                fcdsp::resolve(*entry, f.raw, f.res);
            else
                f.res = fcdsp::Resolution{};
            ++f.resolveSerial;
        }

        // Telemetry (01 §6.2): the stream is fresh while publishCount moves; stale 0.5 s after it stops.
        fcdsp::UiFrame uf;
        if (facade_.readUiFrame(uf) && (!f.hasFrame || uf.publishCount != f.ui.publishCount))
        {
            f.ui = uf;
            f.hasFrame = true;
            f.staleSeconds = 0.0f;
        }
        else if (f.hasFrame && dt > 0.0f)
        {
            f.staleSeconds += dt;
        }
        f.fresh = f.hasFrame && f.staleSeconds < layout::band::kStaleS && !options_.ignoreLive;
        f.live = f.fresh && (f.ui.flags & fcdsp::kUiLive) != 0;

        // Live curves: resolve() + the smoothed fields, unless the audio runs another slot or fades (K1 #7, K2 #24).
        f.eng = f.res.eng;
        f.overlaid = f.live && entry != nullptr && f.ui.modeSlot == f.res.view.slot
                  && (f.ui.flags & fcdsp::kUiFading) == 0;
        if (f.overlaid)
            fcdsp::overlaySmoothed(f.ui, f.eng);
        if (const uint64_t h = hashEng(f.eng); h != f.engHash || !resolvedOnce_)
        {
            f.engHash = h;
            ++f.engSerial;
        }

        // A Mode change (02 §8.7): the landing and the summary key on modeSerial.
        const uint8_t slot = f.res.view.slot;
        if (resolvedOnce_ && slot != lastSlot_)
        {
            f.prevSlot = lastSlot_;
            ++f.modeSerial;
            ++a11yRevision_;
        }
        else if (!resolvedOnce_)
        {
            f.prevSlot = slot;
        }
        lastSlot_ = slot;
        resolvedOnce_ = true;
    }

    void Panel::tick(float dt)
    {
        if (shutDown_)
            return;
        ctx_.seconds += static_cast<double>(dt);
        refreshFrame(dt);
        history_->drain(facade_.history());
        if (fade_ < 1.0f)
            fade_ = funkgui::ease::toward(fade_, 1.0f, dt, layout::chars::kScreenFadeTau, layout::chars::kScreenFadeSnap);
        overlayAmt_ = funkgui::ease::toward(overlayAmt_, overlay_ != Overlay::none ? 1.0f : 0.0f, dt,
                                            layout::browser::kOpenTau, layout::chars::kScreenFadeSnap);
        if (overlayAmt_ <= 0.0f && overlay_ == Overlay::none)
            drawnOverlay_ = Overlay::none;
        preview_->tick(dt);

        ctx_.handNext = HandState{};
        for (std::size_t i = 0; i < views_.size(); ++i)
            if (shown(static_cast<ViewIndex>(i)))
                views_[i]->tick(dt);
        ctx_.hand = ctx_.handNext;
        ticked_ = true;
    }

    void Panel::idle(double nowSec)
    {
        if (gestures_)
            gestures_->poll(nowSec);
    }

    void Panel::drawScreen(funkgui::Canvas& c, Screen s, const funkgui::Theme& th) const
    {
        if (s == Screen::panel)
        {
            view(ViewIndex::slotGrid).draw(c, th);
            view(ViewIndex::band).draw(c, th);
        }
        else
        {
            view(ViewIndex::charScreen).draw(c, th);
        }
    }

    void Panel::draw(funkgui::Canvas& c, const funkgui::Theme& th)
    {
        view(ViewIndex::header).draw(c, th);
        view(ViewIndex::presetStrip).draw(c, th);
        view(ViewIndex::displayRow).draw(c, th);
        if (fade_ < 1.0f && outgoing_ != screen_)
        {
            drawScreen(c, outgoing_, faded(th, 1.0f - fade_));
            drawScreen(c, screen_, faded(th, fade_));
        }
        else
        {
            drawScreen(c, screen_, th);
        }
        view(ViewIndex::footer).draw(c, th);
        if (overlayAmt_ > 0.0f && drawnOverlay_ != Overlay::none)
            view(overlayView(drawnOverlay_)).draw(c, overlayAmt_ >= 1.0f ? th : faded(th, overlayAmt_));
    }

    bool Panel::wantsFullRate() const
    {
        // 02 §9.6: full rate while kUiLive (until the stream goes stale), an ease, a fade or a pending preview; and
        // (ADR-69) while a sub-view has something moving — HISTORY scrolling data in view at wall-clock rate after the
        // audio stops, falling meters and bars, the operating dot's fade — or for layout::live::kActiveS after any input
        // (DisplayRow's activity clock: hover, drag, click, wheel, keys). Idle rate only when nothing moves.
        if (!ticked_ || fade_ < 1.0f || preview_->pending() || ctx_.frame.live)
            return true;
        if (!funkgui::ease::sameBits(overlayAmt_, overlay_ != Overlay::none ? 1.0f : 0.0f))
            return true;
        for (std::size_t i = 0; i < views_.size(); ++i)
            if (shown(static_cast<ViewIndex>(i)) && views_[i]->wantsFullRate())
                return true;
        return false;
    }

    // ---- pointer --------------------------------------------------------------------------------------------------------

    void Panel::pointerMove(const funkgui::PointerEvent& e)
    {
        ctx_.pointer = { e.x, e.y };
        ctx_.pointerIn = true;
        ++ctx_.pointerMoves;
        const int v = hitView(ctx_.pointer);
        if (v != hovered_ && hovered_ >= 0)
            views_[static_cast<std::size_t>(hovered_)]->pointerExit();
        hovered_ = v;
        if (v >= 0)
            views_[static_cast<std::size_t>(v)]->pointerMove(e);
    }

    void Panel::pointerExit()
    {
        ctx_.pointerIn = false;
        if (hovered_ >= 0)
            views_[static_cast<std::size_t>(hovered_)]->pointerExit();
        hovered_ = -1;
    }

    void Panel::pointerDown(const funkgui::PointerEvent& e)
    {
        ctx_.pointer = { e.x, e.y };
        ctx_.pointerIn = true;
        ctx_.pointerPressed = true;
        if (ctx_.pointerMoves == 0)
            ctx_.alwaysChrome = true;                            // a touch screen or tablet (HR :1488)
        ++ctx_.pointerDowns;
        ctx_.focusVisible = false;                               // the pointer is the affordance now (HR)
        if (overlay_ != Overlay::none && !layout::kOverlay.contains(ctx_.pointer))
        {
            closeOverlay();                                      // 02 §8.6: clicking outside cancels
            captured_ = -1;
            return;
        }
        captured_ = hitView(ctx_.pointer);
        if (captured_ >= 0)
            views_[static_cast<std::size_t>(captured_)]->pointerDown(e);
    }

    void Panel::pointerDrag(const funkgui::PointerEvent& e)
    {
        ctx_.pointer = { e.x, e.y };
        if (captured_ >= 0 && live(static_cast<ViewIndex>(captured_)))
            views_[static_cast<std::size_t>(captured_)]->pointerDrag(e);
    }

    void Panel::pointerUp(const funkgui::PointerEvent& e)
    {
        ctx_.pointer = { e.x, e.y };
        ctx_.pointerPressed = false;
        const int v = captured_;
        captured_ = -1;
        if (v >= 0)                                              // even when no longer live: it ends its gesture
            views_[static_cast<std::size_t>(v)]->pointerUp(e);
    }

    void Panel::doubleClick(const funkgui::PointerEvent& e)
    {
        ctx_.pointer = { e.x, e.y };
        const int v = captured_ >= 0 ? captured_ : hitView(ctx_.pointer);
        if (v >= 0 && live(static_cast<ViewIndex>(v)))
            views_[static_cast<std::size_t>(v)]->doubleClick(e);
    }

    bool Panel::wheel(const funkgui::WheelEvent& e)
    {
        ctx_.pointer = { e.x, e.y };
        const int v = hitView(ctx_.pointer);
        return v >= 0 && views_[static_cast<std::size_t>(v)]->wheel(e);
    }

    funkgui::Cursor Panel::cursor() const
    {
        const int v = captured_ >= 0 ? captured_ : hovered_;
        return v >= 0 ? views_[static_cast<std::size_t>(v)]->cursor(ctx_.pointer) : funkgui::Cursor::normal;
    }

    // ---- keyboard -------------------------------------------------------------------------------------------------------

    int Panel::composeFocusOrder(std::span<uint32_t> out) const
    {
        std::size_t n = 0;
        for (const ViewIndex v : kTabOrder)
            if (live(v) && n < out.size())
                n += static_cast<std::size_t>(view(v).focusOrder(out.subspan(n)));
        return static_cast<int>(n);
    }

    bool Panel::moveFocus(int dir)
    {
        std::array<uint32_t, kMaxTabStops> order{};
        const int n = composeFocusOrder(order);
        if (n <= 0)
            return false;
        int i = -1;
        for (int k = 0; k < n; ++k)
            if (order[static_cast<std::size_t>(k)] == ctx_.focus)
                i = k;
        if (i < 0)
            i = dir > 0 ? 0 : n - 1;
        else if (ctx_.focusVisible)
            i = (i + dir + n) % n;                               // a hidden ring reappears where it was
        ctx_.focus = order[static_cast<std::size_t>(i)];
        ctx_.focusVisible = true;
        return true;
    }

    bool Panel::key(const funkgui::KeyEvent& e)
    {
        if (e.key == funkgui::Key::tab)
            return moveFocus(e.mods.shift ? -1 : 1);             // false without a Tab stop: the host keeps Tab
        if (e.key == funkgui::Key::escape)
        {
            // 02 §7.1, §8.9: close an open browser -> hide the focus ring -> leave the screen. The open browser sees Esc
            // first, so a name being typed in the preset browser is cancelled without closing it (U6); the Mode browser
            // never takes it.
            if (overlay_ != Overlay::none)
            {
                if (!view(overlayView(overlay_)).key(e))
                    closeOverlay();
                return true;
            }
            if (ctx_.focusVisible)
            {
                ctx_.focusVisible = false;
                return true;
            }
            if (screen_ == Screen::characteristics)
            {
                setView({ nullptr, Screen::panel, scTab_, Overlay::none }, false);
                return true;
            }
            return false;
        }
        if (overlay_ != Overlay::none && view(overlayView(overlay_)).key(e))
            return true;
        if (ctx_.focus != 0 && ctx_.focusVisible)
        {
            const int v = viewIndexOf(ctx_.focus);
            if (v >= 0 && v < kSubViewCount && live(static_cast<ViewIndex>(v)))
                return views_[static_cast<std::size_t>(v)]->key(e);
        }
        return false;
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void Panel::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        for (std::size_t i = 0; i < views_.size(); ++i)
        {
            const std::size_t first = out.size();
            views_[i]->accessibility(out);
            if (!live(static_cast<ViewIndex>(i)))                // HR skips hidden items; so does the dump (A §1)
                for (std::size_t k = first; k < out.size(); ++k)
                    out[k].visible = false;
        }
    }

    uint32_t Panel::a11yRevision() const
    {
        uint32_t r = a11yRevision_;
        for (const auto& v : views_)
            r += v->a11yRevision();
        return r;
    }

    void Panel::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        const int v = viewIndexOf(id);
        if (v < 0 || v >= kSubViewCount || !live(static_cast<ViewIndex>(v)))
            return;
        if (a == funkgui::A11yAction::focus)
        {
            ctx_.focus = id;
            ctx_.focusVisible = true;
        }
        views_[static_cast<std::size_t>(v)]->a11yAction(id, a, value);
    }

    void Panel::closeGestures()
    {
        if (gestures_)
            gestures_->closeAll();
    }

    // ---- files dropped on the window (S12 lead revision 8): preset files import through the preset browser ------------

    bool Panel::filesInterest(const std::vector<std::string>& files) const
    {
        return static_cast<const PresetBrowser&>(view(ViewIndex::presetBrowser)).filesInterest(files);
    }

    void Panel::filesDropped(const std::vector<std::string>& files)
    {
        if (!shutDown_)
            static_cast<PresetBrowser&>(view(ViewIndex::presetBrowser)).filesDropped(files);
    }
}
