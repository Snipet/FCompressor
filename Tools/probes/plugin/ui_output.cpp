// FCMP_PROBE layer=ui name=output scope=global timeout=60
//
// ui.output (v1.2, ADR-88): the display row's OUTPUT trim (views/OutputTrim.h) over a FakeFacade, driven through
// HeadlessHost input (Panel{skipHint, syncPreview}; settle at 1/60 s; dpi 2, theme 0). Global and spec-only: the trim
// reads nothing Mode-specific.
//
//   a11y.*     a slider "Output" at layout::output::kHit: value "0.0 decibels", v 0, lo −24, hi +24, step 0.5; the
//              display row's first Tab stop, right after the preset strip's
//   drag.*     a drag from the caret, +60 px right, writes +12 dB (240 px per 48 dB); 60 px up the same; Shift +2.4 dB;
//              Cmd +0.48 dB; each drag is ONE gesture outside any batch, and only `output` is written
//   dblclick   a double-click on the trim writes 0 dB
//   wheel.*    a mouse notch (dy +1, not smooth) +4.8 dB; the same reversed −4.8 dB; a smooth dy +1 +1.2 dB
//   keys.*     focused: → +0.5 dB, Shift+→ +0.1, PageUp +3, Home −24, End +24, Delete 0; each one begin/set/end
//   a11y.set   setValue −7.5 writes −7.5 dB; increment +0.5 dB
//   popup      a right-click opens the host's menu and writes nothing
//   spec       the hand over the trim puts "OUTPUT   …" on the footer line
//   text       the value reads "−12.0 DB" (U+2212) at −12 dB
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Text.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/Input.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace O = fcmp::ui::layout::output;
    using fcmp::probe::FakeFacade;
    using fcdsp::Pid;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive
    constexpr double kDbTol = 1.0e-4;

    int b(bool v) { return v ? 1 : 0; }

    uint32_t outputId() { return ui::a11yId(ui::ViewIndex::displayRow, 64); }

    struct Rig
    {
        Rig() : facade("clean"), panel(facade, kProbeOptions), host(panel, 0, 2.0f)
        {
            host.settle(kMaxSettle, kDt);
            facade.resetCounts();
        }
        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        double db() { return static_cast<double>(facade.fakePort(Pid::output).plain()); }
        void   set(float db) { facade.setPlain(Pid::output, db); host.tick(1, kDt); facade.resetCounts(); }
        float  caretX() { return O::kTrackX + fcdsp::toNorm(Pid::output, facade.fakePort(Pid::output).plain()) * O::kTrackW; }
        void   focus() { panel.a11yAction(outputId(), funkgui::A11yAction::focus); host.tick(1, kDt); }
        void   keys(const char* spec) { host.keys(spec); host.tick(1, kDt); }
        // Every logged write since the last reset: all of them `output`, none batched.
        bool onlyOutput() const
        {
            for (const fcmp::probe::FakeWrite& w : facade.writes())
                if (w.pid != Pid::output || w.batchDepth != 0)
                    return false;
            return !facade.writes().empty();
        }
        int gestures() { return facade.fakePort(Pid::output).begins(); }

        FakeFacade            facade;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
    };

    const funkgui::A11yItem* item(const std::vector<funkgui::A11yItem>& v, uint32_t id)
    {
        for (const funkgui::A11yItem& it : v)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    void a11yRows(Probe& P)
    {
        Rig r;
        const std::vector<funkgui::A11yItem> v = r.host.accessibility();
        const funkgui::A11yItem* it = item(v, outputId());
        P.eq("a11y.present", b(it != nullptr), 1);
        if (it == nullptr)
            return;
        P.eq("a11y.role_slider", b(it->role == funkgui::A11yRole::slider), 1);
        P.eq("a11y.title", b(it->title == "Output"), 1);
        P.eq("a11y.value", b(it->value == "0.0 decibels"), 1);
        P.eq("a11y.bounds", b(it->bounds.x == O::kHit.x && it->bounds.y == O::kHit.y && it->bounds.w == O::kHit.w
                              && it->bounds.h == O::kHit.h), 1);
        P.eq("a11y.range", b(it->v == 0.0 && it->lo == -24.0 && it->hi == 24.0 && it->step == 0.5), 1);

        // The Tab order: the first display-row stop comes right after the last preset-strip stop, and it is OUTPUT.
        uint32_t prev = 0, first = 0;
        for (int n = 0; n < 64 && first == 0; ++n)
        {
            r.keys("tab");
            const uint32_t id = r.panel.context().focus;
            if (ui::viewIndexOf(id) == static_cast<int>(ui::ViewIndex::displayRow))
                first = id;
            else
                prev = id;
        }
        P.eq("a11y.first_display_stop", b(first == outputId()), 1);
        P.eq("a11y.after_preset_strip", b(ui::viewIndexOf(prev) == static_cast<int>(ui::ViewIndex::presetStrip)), 1);
    }

    void dragRows(Probe& P)
    {
        const float y = O::kTrackY;
        struct Case { const char* key; float dx, dy; funkgui::Mods mods; double want; };
        const Case cases[] {
            { "drag.right",  60.0f,   0.0f, {},                           12.0 },
            { "drag.up",      0.0f, -60.0f, {},                           12.0 },
            { "drag.shift",  60.0f,   0.0f, { true, false, false, false }, 2.4 },
            { "drag.cmd",    60.0f,   0.0f, { false, true, false, false }, 0.48 },
        };
        for (const Case& c : cases)
        {
            Rig r;
            const float x = r.caretX();
            r.host.drag(x, y, x + c.dx, y + c.dy, 8, c.mods);
            r.host.tick(1, kDt);
            P.near(std::string(c.key) + ".db", r.db(), c.want, kDbTol);
            P.eq(std::string(c.key) + ".one_gesture", r.gestures(), 1);
            P.eq(std::string(c.key) + ".only_output", b(r.onlyOutput()), 1);
        }
        {
            Rig r;
            r.set(-12.0f);
            r.host.doubleClick(r.caretX(), y);
            r.host.tick(1, kDt);
            P.near("dblclick.db", r.db(), 0.0, kDbTol);
        }
        {
            Rig r;
            r.host.click(O::kHit.centreX(), O::kHit.centreY(), funkgui::Mods{ false, false, false, true });   // ctrl
            r.host.tick(1, kDt);
            P.eq("popup.menu_opened", r.host.log.menus, 1);
            P.eq("popup.writes", static_cast<int>(r.facade.writes().size()), 0);
        }
    }

    void wheelRows(Probe& P)
    {
        const float x = O::kHit.centreX(), y = O::kHit.centreY();
        {
            Rig r;
            r.host.wheel(x, y, 1.0f, false);
            r.host.tick(1, kDt);
            P.near("wheel.notch.db", r.db(), 4.8, kDbTol);
            P.eq("wheel.notch.only_output", b(r.onlyOutput()), 1);
        }
        {
            Rig r;
            funkgui::WheelEvent e;
            e.x = x;
            e.y = y;
            e.dy = 1.0f;
            e.reversed = true;
            r.panel.wheel(e);
            r.host.tick(1, kDt);
            P.near("wheel.reversed.db", r.db(), -4.8, kDbTol);
        }
        {
            Rig r;
            r.host.wheel(x, y, 1.0f, true);
            r.host.tick(1, kDt);
            P.near("wheel.smooth.db", r.db(), 1.2, kDbTol);
        }
    }

    void keyRows(Probe& P)
    {
        struct Case { const char* key; const char* spec; float from; double want; };
        const Case cases[] {
            { "keys.right",       "right",       0.0f,  0.5 },
            { "keys.shift_right", "shift+right", 0.0f,  0.1 },
            { "keys.left",        "left",        0.0f, -0.5 },
            { "keys.pageup",      "pageup",      0.0f,  3.0 },
            { "keys.home",        "home",        0.0f, -24.0 },
            { "keys.end",         "end",         0.0f,  24.0 },
            { "keys.delete",      "delete",     -9.0f,  0.0 },
        };
        for (const Case& c : cases)
        {
            Rig r;
            r.set(c.from);
            r.focus();
            r.facade.resetCounts();
            r.keys(c.spec);
            P.near(std::string(c.key) + ".db", r.db(), c.want, kDbTol);
            const fcmp::probe::FakePort& p = r.facade.fakePort(Pid::output);
            P.eq(std::string(c.key) + ".one_tap", b(p.begins() == 1 && p.sets() == 1 && p.ends() == 1), 1);
        }
        {
            Rig r;
            r.panel.a11yAction(outputId(), funkgui::A11yAction::setValue, -7.5);
            r.host.tick(1, kDt);
            P.near("a11y.set.db", r.db(), -7.5, kDbTol);
            r.panel.a11yAction(outputId(), funkgui::A11yAction::increment);
            r.host.tick(1, kDt);
            P.near("a11y.increment.db", r.db(), -7.0, kDbTol);
        }
    }

    void textRows(Probe& P)
    {
        Rig r;
        r.host.move(O::kHit.centreX(), O::kHit.centreY());
        r.host.tick(2, kDt);
        const std::vector<funkgui::A11yItem> v = r.host.accessibility();
        const funkgui::A11yItem* footer = item(v, ui::a11yId(ui::ViewIndex::footer, 1));
        P.eq("spec.footer", b(footer != nullptr && footer->value.rfind("OUTPUT   ", 0) == 0), 1);

        char text[40];
        fcdsp::formatOutput(-12.0f, text, sizeof text);
        P.eq("text.minus12", b(std::string(text) == "\xE2\x88\x92" "12.0 DB"), 1);
        float parsed = 0.0f;
        P.eq("text.parse", b(fcdsp::parseOutput("-3.5 db", parsed) && parsed == -3.5f), 1);
        P.eq("text.parse_clamps", b(fcdsp::parseOutput("\xE2\x88\x92" "40", parsed) && parsed == -24.0f), 1);
        P.eq("text.parse_refuses", b(!fcdsp::parseOutput("loud", parsed)), 1);
    }
}

FCMP_PROBE(ui, output)
{
    (void) C;
    const juce::ScopedJuceInitialiser_GUI juceInit;              // FontService bakes the atlas through JUCE's fonts
    a11yRows(P);
    dragRows(P);
    wheelRows(P);
    keyRows(P);
    textRows(P);
    return P.finish();
}
