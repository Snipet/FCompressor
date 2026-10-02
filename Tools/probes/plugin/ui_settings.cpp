// FCMP_PROBE layer=ui name=settings scope=global timeout=120
//
// ui.settings (v1.2, ADR-85): the settings overlay (views/Settings.h) and the header's gear over a FakeFacade, driven
// through HeadlessHost input (Panel{skipHint, syncPreview}; settle at 1/60 s; dpi 2, theme 0; FCMP_PREFS_DIR is the
// test's sandbox). Global and spec-only: the overlay reads no Mode-specific data beyond the MODE row.
//
//   open.*      the gear opens it (released inside; dragged off it does not); a second click on the gear, a click on the
//               footer and Esc close it; nothing is written by opening or closing; it fades in (full rate) and out.
//   gear.*      the gear is the header's second Tab stop, a button "Settings" at layout::settings::kGear; its hover
//               puts "SETTINGS   …" on the footer line; Return on it opens the overlay with the focus on QUALITY, and
//               closing gives the focus back to the gear.
//   keys.*      while open the Tab order is exactly QUALITY, LOOKAHEAD, SIDECHAIN, NEW QUALITY, NEW LOOKAHEAD, ANIMATION,
//               COPY REPORT, and wraps; → on QUALITY is one tap of `quality`.
//   anim.*      (v1.2, ADR-90) ANIMATION is a stepped slider "Animation" at NORMAL (ease::timeScale() 1): → writes FAST to
//               the preference and the scale is 0.5 at once; End: OFF, 0, and with it the overlay is gone one frame
//               after Esc (at NORMAL it is still fading then); Home: SLOW, 2; a click on its NORMAL label goes back;
//               no parameter is written. The scale follows the preference written elsewhere (another window, or the
//               file the host re-reads when an editor opens) at the next frame. At OFF the TRANSFER curve is the new
//               Mode's on the frame of the switch (at NORMAL it is still easing then).
//   cells.*     a click on HQ, 5 MS and EXTERNAL is one gesture on `quality`, `labudget` and `extkey` each, to the cell's
//               value, outside any batch, and nothing else is written; a click on the selected cell writes nothing.
//   new.*       NEW INSTANCES' cells write the machine preferences kPrefNewQuality / kPrefNewLookahead (UiPreferences)
//               and no parameter.
//   diag.*      the fifteen DIAGNOSTICS rows are staticTexts in order with the FakeFacade's fixed values; after HQ is
//               chosen, OVERSAMPLING, LATENCY and the QUALITY help follow within one refresh (0.25 s); a key bus the host
//               routes is named; the DISPLAY row is HEADLESS; report() is a title line then "KEY: value" per row.
//   copy.*      (web Sprint C, ADR-93) COPY REPORT hands report() to the host (HostServices::copyText; HeadlessHost
//               logs the text and touches no clipboard): one copy of exactly the report; its a11y title reads "Copied"
//               and is "Copy report" again after layout::settings::kCopiedS; a click and Return on the focused cell
//               copy too; over a host without a clipboard nothing is copied and the title stays.
//   cover.*     while open, the slot grid's and the band's items are not visible to accessibility, the footer's are.
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "editor/Tags.h"
#include "editor/views/AnimationModel.h"
#include "editor/views/Settings.h"

#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/core/Ease.h>
#include <funkgui/panel/HeadlessGuiScope.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/panel/Input.h>
#include <funkgui/prefs/UiPreferences.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace L = fcmp::ui::layout;
    using fcmp::probe::FakeFacade;
    using fcdsp::Pid;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive
    const std::string kDot = " \xC2\xB7 ";                              // " · "

    int b(bool v) { return v ? 1 : 0; }

    struct Rig
    {
        Rig() : facade("clean"), panel(facade, kProbeOptions), host(panel, 0, 2.0f)
        {
            host.settle(kMaxSettle, kDt);
            facade.resetCounts();
        }
        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        const ui::PanelContext& ctx() const { return panel.context(); }
        bool open() const { return panel.overlay() == ui::Overlay::settings; }
        int  settle() { return host.settle(kMaxSettle, kDt); }
        void click(const funkgui::Rect& r)
        {
            host.click(r.centreX(), r.centreY());
            host.tick(1, kDt);
        }
        void keys(const char* spec)
        {
            host.keys(spec);
            host.tick(1, kDt);
        }
        void openByGear()
        {
            click(L::settings::kGear);
            settle();
        }
        std::vector<funkgui::A11yItem> items() const { return host.accessibility(); }

        FakeFacade            facade;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
    };

    // The probe reaches the Panel's Settings through its a11y ids only; report() and copyReport() it calls on a Settings
    // of its own over the same context (the Panel's sub-views are private).
    const funkgui::A11yItem* item(const std::vector<funkgui::A11yItem>& v, uint32_t id)
    {
        for (const funkgui::A11yItem& it : v)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    uint32_t sid(uint32_t local) { return ui::a11yId(ui::ViewIndex::settings, local); }

    std::string rowValue(const Rig& r, int row)
    {
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* it = item(v, sid(ui::Settings::kDiagLocal0 + static_cast<uint32_t>(row)));
        return it != nullptr ? it->value : std::string("<missing>");
    }

    // The cell i of a radioGroup (a11y local + 1 + i): its bounds.
    funkgui::Rect cellOf(const Rig& r, uint32_t groupLocal, int i)
    {
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* it = item(v, sid(groupLocal + 1u + static_cast<uint32_t>(i)));
        return it != nullptr ? it->bounds : funkgui::Rect{};
    }

    int writesTotal(const FakeFacade& f) { return static_cast<int>(f.writes().size()); }

    std::string footerLine(const Rig& r)
    {
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* it = item(v, ui::a11yId(ui::ViewIndex::footer, 1));
        return it != nullptr ? it->value : std::string();
    }

    // ---- opening and closing --------------------------------------------------------------------------------------------

    void openRows(Probe& P)
    {
        Rig r;
        r.host.click(L::settings::kGear.centreX(), L::settings::kGear.centreY());
        r.host.tick(1, kDt);
        const bool fading = r.open() && r.panel.wantsFullRate();
        r.settle();
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* q = item(v, sid(ui::Settings::kQualityLocal));
        P.eq("open.gear_click", b(r.open() && q != nullptr && q->visible), 1);
        P.eq("open.fades_in", b(fading), 1);
        r.click(L::settings::kGear);                              // above the overlay: outside it, so it closes
        r.settle();
        P.eq("open.gear_again_closes", b(!r.open()), 1);
        r.openByGear();
        r.click({ 100.0f, 604.0f, 1.0f, 1.0f });                  // the footer's line
        r.settle();
        P.eq("open.footer_click_closes", b(!r.open()), 1);
        r.openByGear();
        r.keys("escape");
        r.settle();
        P.eq("open.escape_closes", b(!r.open()), 1);
        // Dragging off the gear does not open it.
        r.host.drag(L::settings::kGear.centreX(), L::settings::kGear.centreY(), 400.0f, 30.0f, 4);
        r.settle();
        P.eq("open.drag_off_cancels", b(!r.open()), 1);
        P.eq("open.writes_nothing", writesTotal(r.facade) + r.facade.batches(), 0);
    }

    // ---- the gear ---------------------------------------------------------------------------------------------------------

    void gearRows(Probe& P)
    {
        Rig r;
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* g = item(v, ui::a11yId(ui::ViewIndex::header, 2));
        P.eq("gear.a11y_button", b(g != nullptr && g->role == funkgui::A11yRole::button && g->title == "Settings"
                                   && g->bounds.x == L::settings::kGear.x && g->bounds.w == L::settings::kGear.w), 1);
        r.host.move(L::settings::kGear.centreX(), L::settings::kGear.centreY());
        r.host.tick(1, kDt);
        P.eq("gear.hover_spec", b(footerLine(r).rfind("SETTINGS   ", 0) == 0), 1);
        r.host.move(480.0f, 460.0f);
        r.keys("tab");                                            // the Mode latch (the panel's first stop)
        r.keys("tab");                                            // the gear
        const bool second = r.ctx().focus == ui::a11yId(ui::ViewIndex::header, 2);
        P.eq("gear.second_tab_stop", b(second), 1);
        r.keys("return");
        r.settle();
        P.eq("gear.return_opens_on_quality", b(r.open() && r.ctx().focus == sid(ui::Settings::kQualityLocal)), 1);
        r.keys("escape");
        r.settle();
        P.eq("gear.close_returns_focus", b(!r.open() && r.ctx().focus == ui::a11yId(ui::ViewIndex::header, 2)), 1);
    }

    // ---- keys -------------------------------------------------------------------------------------------------------------

    void keyRows(Probe& P)
    {
        Rig r;
        r.openByGear();
        const std::array<uint32_t, 7> want { sid(ui::Settings::kQualityLocal), sid(ui::Settings::kBudgetLocal),
                                             sid(ui::Settings::kKeyLocal), sid(ui::Settings::kNewQualityLocal),
                                             sid(ui::Settings::kNewBudgetLocal), sid(ui::Settings::kAnimationLocal),
                                             sid(ui::Settings::kCopyLocal) };
        std::vector<uint32_t> stops;
        for (int i = 0; i < 9; ++i)
        {
            r.keys("tab");
            stops.push_back(r.ctx().focus);
        }
        bool order = stops.size() == 9;
        for (std::size_t i = 0; order && i < want.size(); ++i)
            order = stops[i] == want[i];
        P.eq("keys.tab_order", b(order), 1);
        P.eq("keys.tab_wraps", b(order && stops[7] == want[0] && stops[8] == want[1]), 1);
        // Back on QUALITY (STD): → is one tap to HQ.
        while (r.ctx().focus != want[0])
            r.keys("tab");
        r.facade.resetCounts();
        r.keys("right");
        const fcmp::probe::FakePort& q = r.facade.fakePort(Pid::quality);
        P.eq("keys.right_taps_quality", b(q.begins() == 1 && q.ends() == 1 && q.plain() == 2.0f), 1);
    }

    // ---- ANIMATION (ADR-90) -----------------------------------------------------------------------------------------------

    void animRows(Probe& P)
    {
        Rig r;
        r.openByGear();
        const uint32_t id = sid(ui::Settings::kAnimationLocal);
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* it = item(v, id);
        P.eq("anim.slider", b(it != nullptr && it->role == funkgui::A11yRole::slider && it->title == "Animation"), 1);
        P.eq("anim.default_normal", b(ui::AnimationModel::index() == 1 && funkgui::ease::timeScale() == 1.0f), 1);
        r.panel.a11yAction(id, funkgui::A11yAction::focus);
        r.host.tick(1, kDt);
        r.facade.clearWrites();
        r.keys("right");
        P.eq("anim.right_fast", b(ui::AnimationModel::index() == 2 && funkgui::ease::timeScale() == 0.5f), 1);
        r.keys("end");
        P.eq("anim.end_off", b(ui::AnimationModel::index() == 4 && funkgui::ease::timeScale() == 0.0f), 1);
        // The overlay one frame after Esc: gone at OFF, still fading at NORMAL (its ground is drawn while it fades).
        const auto groundAfterEsc = [&r]() {
            r.host.keys("escape");                               // the focus ring goes first ...
            r.host.keys("escape");                               // ... then the overlay
            r.host.tick(1, kDt);
            int n = 0;
            for (const funkgui::Prim& p : r.host.draw().prims)
                n += p.tag == ui::tag::settingsBg ? 1 : 0;
            return n;
        };
        P.eq("anim.off_gone_after_one_frame", b(groundAfterEsc() == 0), 1);
        {
            Rig n;
            n.openByGear();
            n.panel.a11yAction(id, funkgui::A11yAction::focus);
            n.host.tick(1, kDt);
            n.keys("home");
            n.keys("right");                                     // NORMAL
            n.host.keys("escape");
            n.host.keys("escape");
            n.host.tick(1, kDt);
            int bg = 0;
            for (const funkgui::Prim& p : n.host.draw().prims)
                bg += p.tag == ui::tag::settingsBg ? 1 : 0;
            P.eq("anim.normal_fading_after_one_frame", b(bg > 0), 1);
        }
        r.openByGear();
        r.panel.a11yAction(id, funkgui::A11yAction::focus);
        r.host.tick(1, kDt);
        r.keys("home");
        P.eq("anim.home_slow", b(ui::AnimationModel::index() == 0 && funkgui::ease::timeScale() == 2.0f), 1);
        const float normalX = L::settings::kAnimation.x + L::settings::kAnimation.w * 1.5f / 5.0f;
        r.host.click(normalX, L::settings::kAnimation.subTop() + 4.0f);
        r.host.tick(1, kDt);
        P.eq("anim.label_click_normal", b(ui::AnimationModel::index() == 1 && funkgui::ease::timeScale() == 1.0f), 1);
        P.eq("anim.no_parameter_written", writesTotal(r.facade), 0);

        // The preference written elsewhere (another window in this process, or UiPreferences::reload()).
        funkgui::UiPreferences::get().setInt(L::settings::kPrefAnimation, 3);
        r.host.tick(1, kDt);
        P.eq("anim.follows_prefs", b(funkgui::ease::timeScale() == 0.25f), 1);
        funkgui::UiPreferences::get().setInt(L::settings::kPrefAnimation, 1);
        r.host.tick(1, kDt);
        P.eq("anim.follows_prefs_back", b(funkgui::ease::timeScale() == 1.0f), 1);

        // The TRANSFER curve on a Mode switch (THRESHOLD moved with it, so the curves differ): its prims two frames
        // after the switch against 0.3 s after it (the ease takes 160 ms at NORMAL).
        const auto curveLands = [](int index) {
            funkgui::UiPreferences::get().setInt(L::settings::kPrefAnimation, index);
            Rig m;
            m.facade.setMode("bus-g");
            m.facade.setPlain(Pid::thr, -40.0f);                 // with it a curve of another shape
            m.host.tick(2, kDt);
            const auto curve = [&m]() {
                std::vector<funkgui::Prim> out;
                for (const funkgui::Prim& p : m.host.draw().prims)
                    if (p.tag == ui::tag::transferCurve)
                        out.push_back(p);
                return out;
            };
            const std::vector<funkgui::Prim> first = curve();
            m.host.tick(18, kDt);
            const std::vector<funkgui::Prim> later = curve();
            return !first.empty() && first.size() == later.size()
                   && std::memcmp(first.data(), later.data(), first.size() * sizeof(funkgui::Prim)) == 0;
        };
        P.eq("anim.curve_off_lands_at_once", b(curveLands(4)), 1);
        P.eq("anim.curve_normal_eases", b(!curveLands(1)), 1);
        funkgui::ease::setTimeScale(1.0f);
    }

    // ---- the cells --------------------------------------------------------------------------------------------------------

    void cellRows(Probe& P)
    {
        Rig r;
        r.openByGear();
        const auto one = [&](uint32_t local, int cell, Pid pid, float plain, const char* key) {
            r.facade.resetCounts();
            r.facade.clearWrites();
            r.click(cellOf(r, local, cell));
            const fcmp::probe::FakePort& port = r.facade.fakePort(pid);
            bool only = true;
            for (const fcmp::probe::FakeWrite& w : r.facade.writes())
                only = only && w.pid == pid && w.inGesture && w.batchDepth == 0;
            P.eq(std::string("cells.") + key, b(port.begins() == 1 && port.ends() == 1 && !port.inGesture()
                                                && port.plain() == plain && only && !r.facade.writes().empty()
                                                && r.facade.batches() == 0), 1);
        };
        one(ui::Settings::kQualityLocal, 2, Pid::quality, 2.0f, "quality_hq");
        one(ui::Settings::kBudgetLocal, 1, Pid::labudget, 1.0f, "budget_5ms");
        one(ui::Settings::kKeyLocal, 1, Pid::extkey, 1.0f, "sidechain_external");
        r.facade.resetCounts();
        r.facade.clearWrites();
        r.click(cellOf(r, ui::Settings::kQualityLocal, 2));       // already HQ
        P.eq("cells.selected_writes_nothing", writesTotal(r.facade), 0);
        P.eq("cells.overlay_stays_open", b(r.open()), 1);
    }

    // ---- NEW INSTANCES -----------------------------------------------------------------------------------------------

    void newRows(Probe& P)
    {
        Rig r;
        r.openByGear();
        funkgui::UiPreferences& prefs = funkgui::UiPreferences::get();
        r.facade.clearWrites();
        r.click(cellOf(r, ui::Settings::kNewQualityLocal, 2));    // HQ
        r.click(cellOf(r, ui::Settings::kNewBudgetLocal, 2));     // 20 MS
        P.eq("new.quality_pref", prefs.getInt(fcmp::kPrefNewQuality, -1, -1, 9), 2);
        P.eq("new.lookahead_pref", prefs.getInt(fcmp::kPrefNewLookahead, -1, -1, 9), 2);
        P.eq("new.no_parameter_write", writesTotal(r.facade) + r.facade.batches(), 0);
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* hq = item(v, sid(ui::Settings::kNewQualityLocal + 3u));
        P.eq("new.cell_checked", b(hq != nullptr && hq->checked), 1);
        prefs.setInt(fcmp::kPrefNewQuality, 1);                   // leave the sandbox as the other rows expect it
        prefs.setInt(fcmp::kPrefNewLookahead, 0);
    }

    // ---- DIAGNOSTICS ------------------------------------------------------------------------------------------------------

    void diagRows(Probe& P)
    {
        Rig r;
        r.openByGear();
        const std::vector<funkgui::A11yItem> v = r.items();
        const std::array<const char*, ui::Settings::kDiagRows> titles {
            "Version", "Libraries", "Format and host", "Sample rate", "Block size", "Channels", "Oversampling",
            "Lookahead", "Latency", "DSP load", "Overruns", "Audio", "Mode", "Presets", "Display" };
        bool ordered = true;
        for (std::size_t i = 0; i < titles.size(); ++i)
        {
            const funkgui::A11yItem* it = item(v, sid(ui::Settings::kDiagLocal0 + static_cast<uint32_t>(i)));
            ordered = ordered && it != nullptr && it->role == funkgui::A11yRole::staticText && it->title == titles[i]
                   && it->visible;
        }
        P.eq("diag.rows_in_order", b(ordered), 1);
        const int stdLatency = fcdsp::kOs[1].latency;
        P.eq("diag.version", b(rowValue(r, 0) == "FCOMPRESSOR 0.0.0"), 1);
        P.eq("diag.libraries", b(rowValue(r, 1) == "FUNKGUI 0.0.0" + kDot + "JUCE 0.0.0"), 1);
        P.eq("diag.format_unknown", b(rowValue(r, 2) == "UNKNOWN FORMAT" + kDot + "UNKNOWN HOST"), 1);
        P.eq("diag.rate", b(rowValue(r, 3) == "48 000 HZ"), 1);
        P.eq("diag.block", b(rowValue(r, 4) == "UP TO 512 SAMPLES"), 1);
        P.eq("diag.channels", b(rowValue(r, 5) == "2 IN" + kDot + "2 OUT" + kDot + "NO KEY INPUT"), 1);
        P.eq("diag.oversampling", b(rowValue(r, 6) == "2\xC3\x97 IIR, RUNS AT 96 000 HZ"), 1);
        P.eq("diag.lookahead_off", b(rowValue(r, 7) == "OFF"), 1);
        P.eq("diag.latency", b(rowValue(r, 8) == std::to_string(stdLatency) + " SAMPLES" + kDot + "0.08 MS"), 1);
        P.eq("diag.load", b(rowValue(r, 9) == "3.1 %" + kDot + "PEAK 7.8 %"), 1);
        P.eq("diag.overruns", b(rowValue(r, 10) == "0 OF 1000 BLOCKS"), 1);
        P.eq("diag.audio_none", b(rowValue(r, 11) == "NO AUDIO SINCE THIS WINDOW OPENED"), 1);
        P.eq("diag.mode", b(rowValue(r, 12).rfind("CLEAN" + kDot + "SLOT 0" + kDot + "REVISION ", 0) == 0), 1);
        P.eq("diag.display_headless", b(rowValue(r, 14) == "HEADLESS"), 1);

        // HQ and 5 MS: the rows follow within one refresh.
        r.click(cellOf(r, ui::Settings::kQualityLocal, 2));
        r.click(cellOf(r, ui::Settings::kBudgetLocal, 1));
        r.host.tick(20, kDt);
        const int la = fcdsp::lookaheadSamples(fcdsp::LookaheadBudget::ms5, 48000.0);
        const int total = fcdsp::kOs[2].latency + la;
        P.eq("diag.follows_quality", b(rowValue(r, 6) == "4\xC3\x97 FIR, RUNS AT 192 000 HZ"), 1);
        P.eq("diag.follows_lookahead", b(rowValue(r, 7) == "5 MS BUDGET, " + std::to_string(la) + " SAMPLES"), 1);
        P.eq("diag.follows_latency", b(rowValue(r, 8).rfind(std::to_string(total) + " SAMPLES", 0) == 0), 1);

        // A key bus the host routes.
        fcmp::Diagnostics d = r.facade.diagnostics();
        d.keyChans = 2;
        r.facade.setDiagnostics(d);
        r.host.tick(20, kDt);
        const std::vector<funkgui::A11yItem> w = r.items();
        const funkgui::A11yItem* note = item(w, sid(ui::Settings::kKeyNoteLocal));
        P.eq("diag.key_note", b(note != nullptr && note->value == "KEY INPUT: STEREO FROM THE HOST"), 1);
        P.eq("diag.key_channels", b(rowValue(r, 5) == "2 IN" + kDot + "2 OUT" + kDot + "KEY 2"), 1);
        r.facade.setDiagnostics(std::nullopt);
    }

    // ---- report and COPY REPORT (a Settings of the probe's own over the Panel's context) -----------------------------

    // A host that serves less than HeadlessHost: every call goes on to it, except that services() loses the `without`
    // bits and a call for a service that is not reported refuses, as HostServices' defaults do (`refused` counts
    // them). While it lives the Panel is attached to it; input, ticks and frames stay the HeadlessHost's.
    class LesserHost final : public funkgui::HostServices
    {
    public:
        LesserHost(ui::Panel& panel, funkgui::HeadlessHost& inner, unsigned without)
            : panel_(panel), inner_(inner), without_(without)
        {
            panel_.attach(*this);
        }
        ~LesserHost() override { panel_.attach(inner_); }

        LesserHost(const LesserHost&) = delete;
        LesserHost& operator=(const LesserHost&) = delete;

        void   setUnboundedDrag(bool on) override { inner_.setUnboundedDrag(on); }
        void   showParamMenu(funkgui::ParamPort& p, float x, float y) override { inner_.showParamMenu(p, x, y); }
        void   nudgeFullRate() override { inner_.nudgeFullRate(); }
        double nowSeconds() const override { return inner_.nowSeconds(); }
        void   beginBatch() override { inner_.beginBatch(); }
        void   endBatch() override { inner_.endBatch(); }
        int    themeIndex() const override { return inner_.themeIndex(); }
        int    zoomPercent() const override { return inner_.zoomPercent(); }
        void   setZoomPercent(int percent) override { inner_.setZoomPercent(percent); }
        std::span<const int> zoomSteps() const override { return inner_.zoomSteps(); }
        bool   zoomFits(int percent) const override { return inner_.zoomFits(percent); }
        unsigned services() const override { return inner_.services() & ~without_; }
        bool   showMenu(const funkgui::MenuRequest& request, funkgui::MenuCallback done) override
        {
            return serves(funkgui::hostservice::menus) && inner_.showMenu(request, std::move(done));
        }
        void   dismissMenus() override { inner_.dismissMenus(); }
        bool   chooseFiles(const funkgui::FileRequest& request, funkgui::FilesCallback done) override
        {
            return serves(funkgui::hostservice::fileChooser) && inner_.chooseFiles(request, std::move(done));
        }
        bool   copyText(std::string_view utf8) override
        {
            return serves(funkgui::hostservice::clipboard) && inner_.copyText(utf8);
        }
        bool   commandKeyIsMeta() const override { return inner_.commandKeyIsMeta(); }

        int refused = 0;

    private:
        bool serves(unsigned service)
        {
            if ((without_ & service) == 0u)
                return true;
            ++refused;
            return false;
        }

        ui::Panel&             panel_;
        funkgui::HeadlessHost& inner_;
        unsigned               without_;
    };

    std::string copyTitle(const Rig& r)
    {
        const std::vector<funkgui::A11yItem> v = r.items();
        const funkgui::A11yItem* c = item(v, sid(ui::Settings::kCopyLocal));
        return c != nullptr ? c->title : std::string("<missing>");
    }

    void reportRows(Probe& P)
    {
        Rig r;
        r.openByGear();
        ui::Settings own(const_cast<ui::PanelContext&>(r.ctx()));
        const std::string rep = own.report();
        P.eq("report.title", b(rep.rfind("FCompressor diagnostics\n", 0) == 0), 1);
        P.eq("report.rows", b(rep.find("SAMPLE RATE: 48 000 HZ\n") != std::string::npos
                              && rep.find("DISPLAY: HEADLESS\n") != std::string::npos), 1);
        int lines = 0;
        for (const char ch : rep)
            lines += ch == '\n' ? 1 : 0;
        P.eq("report.line_count", lines, 1 + ui::Settings::kDiagRows);

        // COPY REPORT through the host: one copy, of exactly the report.
        const bool copied = own.copyReport();
        P.eq("copy.report_copied", b(copied && r.host.log.copies == 1 && r.host.log.lastCopy == own.report()
                                     && r.host.log.lastCopy.rfind("FCompressor diagnostics\n", 0) == 0
                                     && r.host.log.lastCopy.find("SAMPLE RATE: 48 000 HZ\n") != std::string::npos), 1);
        // The Panel's own cell (a11y press): it says COPIED, for kCopiedS of panel time.
        const bool before = copyTitle(r) == "Copy report";
        r.panel.a11yAction(sid(ui::Settings::kCopyLocal), funkgui::A11yAction::press, 0.0);
        r.host.tick(1, kDt);
        P.eq("copy.title_copied", b(before && r.host.log.copies == 2 && copyTitle(r) == "Copied"
                                    && r.panel.wantsFullRate()), 1);
        r.host.tick(static_cast<int>(L::settings::kCopiedS * 60.0) + 2, kDt);
        P.eq("copy.title_back", b(copyTitle(r) == "Copy report" && r.host.log.copies == 2), 1);
        // A click on the cell, and Return on it focused, copy too.
        funkgui::Rect cell{};
        for (const funkgui::A11yItem& it : r.items())
            if (it.id == sid(ui::Settings::kCopyLocal))
                cell = it.bounds;
        r.click(cell);
        const bool clicked = r.host.log.copies == 3 && copyTitle(r) == "Copied";
        r.panel.a11yAction(sid(ui::Settings::kCopyLocal), funkgui::A11yAction::focus, 0.0);
        r.keys("return");
        P.eq("copy.click_and_key", b(clicked && r.host.log.copies == 4 && r.host.log.lastCopy == own.report()), 1);
    }

    // A host without a clipboard: nothing is copied and nothing is said (what a headless run did before the host
    // served the clipboard).
    void noClipboardRows(Probe& P)
    {
        Rig r;
        LesserHost lesser(r.panel, r.host, funkgui::hostservice::clipboard);
        r.openByGear();
        ui::Settings own(const_cast<ui::PanelContext&>(r.ctx()));
        const bool refused = !own.copyReport();
        r.panel.a11yAction(sid(ui::Settings::kCopyLocal), funkgui::A11yAction::press, 0.0);
        r.host.tick(1, kDt);
        P.eq("copy.no_clipboard", b(refused && lesser.refused == 2 && r.host.log.copies == 0
                                    && r.host.log.lastCopy.empty() && copyTitle(r) == "Copy report"), 1);
    }

    // ---- what the overlay covers ----------------------------------------------------------------------------------------

    void coverRows(Probe& P)
    {
        Rig r;
        r.openByGear();
        int slotsVisible = 0, footerVisible = 0, settingsVisible = 0;
        for (const funkgui::A11yItem& it : r.items())
        {
            const int view = ui::viewIndexOf(it.id);
            if (view == static_cast<int>(ui::ViewIndex::slotGrid) || view == static_cast<int>(ui::ViewIndex::band))
                slotsVisible += it.visible ? 1 : 0;
            if (view == static_cast<int>(ui::ViewIndex::footer))
                footerVisible += it.visible ? 1 : 0;
            if (view == static_cast<int>(ui::ViewIndex::settings))
                settingsVisible += it.visible ? 1 : 0;
        }
        P.eq("cover.slots_hidden", slotsVisible, 0);
        P.ge("cover.footer_visible", footerVisible, 1);
        P.ge("cover.settings_visible", settingsVisible, 20);
        // A click on the overlay where the band is underneath writes nothing and keeps it open.
        r.facade.resetCounts();
        r.facade.clearWrites();
        r.click({ 300.0f, 500.0f, 1.0f, 1.0f });
        P.eq("cover.click_through_nothing", b(r.open() && writesTotal(r.facade) == 0), 1);
    }
}

FCMP_PROBE(ui, settings)
{
    (void) C;
    const funkgui::HeadlessGuiScope gui;                          // what FontService needs before it bakes the atlas
    openRows(P);
    gearRows(P);
    keyRows(P);
    animRows(P);
    cellRows(P);
    newRows(P);
    diagRows(P);
    reportRows(P);
    noClipboardRows(P);
    coverRows(P);
    return P.finish();
}
