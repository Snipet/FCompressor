// FCMP_PROBE layer=ui name=entry scope=global timeout=90
//
// ui.entry (v1.2, ADR-89): typed values — views/ValueEntry.h over the slot grid and the OUTPUT trim — on a FakeFacade,
// driven through HeadlessHost input (Panel{skipHint, syncPreview}; settle at 1/60 s; dpi 2, theme 0). Clean unless a row
// names another Mode. Spec-only.
//
//   slot.return.*      Return on the focused THRESHOLD opens a field (PanelContext::textEntry is the slot grid) holding
//                      the value selected: "-24.5" Return writes −24.5 dB in ONE tap, closes it and keeps the focus
//   slot.digit.*       a typed '-' opens it starting with the character: "-30" Return writes −30 dB
//   slot.click.*       a press and release on RATIO (no drag) focuses it with the ring hidden, then "8" Return writes 8:1
//   slot.refuse.*      "abc" Return writes nothing and the field stays; Esc then closes it, writes nothing, and neither
//                      hides the ring nor leaves the screen
//   slot.tab.*         "-6" Tab writes −6 dB, closes the field and moves the focus to the next stop
//   slot.elsewhere.*   "-12", then a click on the band: −12 dB written, the field closed
//   slot.inside.*      a double-click inside an open field neither resets the slot nor closes the field
//   slot.units.*       "-20 db" is −20 dB; ATTACK "500 us" is 0.5 ms
//   slot.refused.*     Return on DRIVE, n/a in Clean with VOICE OFF, opens nothing and writes nothing
//   slot.mode.*        a Mode change under an open field closes it without writing
//   slot.stepped.*     Bus G's RATIO (2 · 4 · 10): "10" Return writes the 10:1 step (S 0.9)
//   output.*           Return on the focused OUTPUT, "-3" Return: −3 dB in one tap; a press on OUTPUT then "6" Return:
//                      +6 dB
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/Input.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
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
    constexpr double kTol = 1.0e-4;

    int b(bool v) { return v ? 1 : 0; }

    // Slot a11y ids: (slotGrid << 16) | (1 + the slot's index in layout::kSlots).
    uint32_t slotId(Pid pid)
    {
        for (std::size_t i = 0; i < L::kSlots.size(); ++i)
            if (L::kSlots[i].pid == pid)
                return ui::a11yId(ui::ViewIndex::slotGrid, static_cast<uint32_t>(1 + i));
        return 0;
    }
    uint32_t outputId() { return ui::a11yId(ui::ViewIndex::displayRow, 64); }

    struct Rig
    {
        explicit Rig(std::string_view mode = "clean") : facade(mode), panel(facade, kProbeOptions), host(panel, 0, 2.0f)
        {
            host.settle(kMaxSettle, kDt);
            facade.resetCounts();
        }
        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        const ui::PanelContext& ctx() const { return panel.context(); }
        bool   open() const { return ctx().textEntry >= 0; }
        double raw(Pid p) { return static_cast<double>(facade.fakePort(p).plain()); }
        void   focus(uint32_t id) { panel.a11yAction(id, funkgui::A11yAction::focus); host.tick(1, kDt); }
        void   keys(const char* spec) { host.keys(spec); host.tick(1, kDt); }
        int    writes() const { return static_cast<int>(facade.writes().size()); }
        bool   oneTap(Pid p) { const auto& q = facade.fakePort(p); return q.begins() == 1 && q.sets() == 1 && q.ends() == 1; }
        funkgui::Rect bounds(uint32_t id) const
        {
            for (const funkgui::A11yItem& it : host.accessibility())
                if (it.id == id)
                    return it.bounds;
            return {};
        }

        FakeFacade            facade;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
    };

    void slotRows(Probe& P)
    {
        {
            Rig r;
            r.focus(slotId(Pid::thr));
            r.facade.resetCounts();
            r.keys("return");
            P.eq("slot.return.opens", b(r.open() && r.ctx().textEntry == static_cast<int>(ui::ViewIndex::slotGrid)), 1);
            r.keys("-,2,4,.,5,return");                          // the value was selected: typing replaces it
            P.near("slot.return.db", r.raw(Pid::thr), -24.5, kTol);
            P.eq("slot.return.one_tap", b(r.oneTap(Pid::thr)), 1);
            P.eq("slot.return.closed", b(!r.open()), 1);
            P.eq("slot.return.focus_kept", b(r.ctx().focus == slotId(Pid::thr) && r.ctx().focusVisible), 1);
        }
        {
            Rig r;
            r.focus(slotId(Pid::thr));
            r.keys("-");
            P.eq("slot.digit.opens", b(r.open()), 1);
            r.keys("3,0,return");
            P.near("slot.digit.db", r.raw(Pid::thr), -30.0, kTol);
        }
        {
            Rig r;
            const funkgui::Rect s = r.bounds(slotId(Pid::ratio));
            r.host.click(s.centreX(), s.y + 20.0f);
            r.host.tick(1, kDt);
            P.eq("slot.click.focused", b(r.ctx().focus == slotId(Pid::ratio) && !r.ctx().focusVisible), 1);
            r.facade.resetCounts();
            r.keys("8,return");
            P.near("slot.click.ratio_s", r.raw(Pid::ratio), 1.0 - 1.0 / 8.0, kTol);
        }
        {
            Rig r;
            r.focus(slotId(Pid::thr));
            r.keys("return");
            r.facade.resetCounts();
            r.keys("a,b,c,return");
            P.eq("slot.refuse.writes", r.writes(), 0);
            P.eq("slot.refuse.still_open", b(r.open()), 1);
            r.keys("escape");
            P.eq("slot.refuse.esc_closes", b(!r.open()), 1);
            P.eq("slot.refuse.esc_writes", r.writes(), 0);
            P.eq("slot.refuse.esc_keeps_ring", b(r.ctx().focusVisible), 1);
            P.eq("slot.refuse.esc_keeps_screen", b(r.panel.screen() == ui::Screen::panel), 1);
        }
        {
            Rig r;
            r.focus(slotId(Pid::thr));
            r.keys("return");
            r.keys("-,6,tab");
            P.near("slot.tab.db", r.raw(Pid::thr), -6.0, kTol);
            P.eq("slot.tab.closed", b(!r.open()), 1);
            P.eq("slot.tab.focus_moved", b(r.ctx().focus == slotId(Pid::ratio)), 1);
        }
        {
            Rig r;
            r.focus(slotId(Pid::thr));
            r.keys("return");
            r.keys("-,1,2");
            r.host.click(L::kBand.x + 300.0f, L::kBand.y + 100.0f);
            r.host.tick(1, kDt);
            P.near("slot.elsewhere.db", r.raw(Pid::thr), -12.0, kTol);
            P.eq("slot.elsewhere.closed", b(!r.open()), 1);
        }
        {
            Rig r;
            r.facade.setPlain(Pid::thr, -40.0f);
            r.host.tick(1, kDt);
            r.focus(slotId(Pid::thr));
            r.keys("return");
            r.facade.resetCounts();
            const funkgui::Rect box = r.ctx().textEntryBox;
            r.host.doubleClick(box.centreX(), box.centreY());
            r.host.tick(1, kDt);
            P.eq("slot.inside.no_write", r.writes(), 0);
            P.eq("slot.inside.still_open", b(r.open()), 1);
        }
        {
            Rig r;
            r.focus(slotId(Pid::thr));
            r.keys("return");
            r.keys("-,2,0,space,d,b,return");
            P.near("slot.units.db", r.raw(Pid::thr), -20.0, kTol);
            r.focus(slotId(Pid::atk));
            r.keys("return");
            r.keys("5,0,0,space,u,s,return");
            P.near("slot.units.us", r.raw(Pid::atk), 0.5, kTol);
        }
        {
            Rig r;
            r.focus(slotId(Pid::drive));
            r.facade.resetCounts();
            r.keys("return");
            r.keys("5");
            P.eq("slot.refused.closed", b(!r.open()), 1);
            P.eq("slot.refused.writes", r.writes(), 0);
        }
        {
            Rig r;
            r.focus(slotId(Pid::thr));
            r.keys("return");
            r.facade.resetCounts();
            r.facade.setMode("bus-g");
            r.host.tick(2, kDt);
            P.eq("slot.mode.closed", b(!r.open()), 1);
            P.eq("slot.mode.writes", r.writes(), 0);
        }
        {
            Rig r("bus-g");
            r.focus(slotId(Pid::ratio));
            r.keys("return");
            r.keys("1,0,return");
            P.near("slot.stepped.ratio_s", r.raw(Pid::ratio), 0.9, kTol);
        }
    }

    void outputRows(Probe& P)
    {
        {
            Rig r;
            r.focus(outputId());
            r.facade.resetCounts();
            r.keys("return");
            P.eq("output.return.opens", b(r.ctx().textEntry == static_cast<int>(ui::ViewIndex::displayRow)), 1);
            r.keys("-,3,return");
            P.near("output.return.db", r.raw(Pid::output), -3.0, kTol);
            P.eq("output.return.one_tap", b(r.oneTap(Pid::output)), 1);
            P.eq("output.return.closed", b(!r.open()), 1);
        }
        {
            Rig r;
            r.host.click(L::output::kHit.centreX(), L::output::kHit.centreY());
            r.host.tick(1, kDt);
            r.keys("6,return");
            P.near("output.click.db", r.raw(Pid::output), 6.0, kTol);
        }
    }
}

FCMP_PROBE(ui, entry)
{
    (void) C;
    const juce::ScopedJuceInitialiser_GUI juceInit;              // FontService bakes the atlas through JUCE's fonts
    slotRows(P);
    outputRows(P);
    return P.finish();
}
