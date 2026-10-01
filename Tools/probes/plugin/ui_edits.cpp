// FCMP_PROBE layer=ui name=edits scope=global timeout=60
//
// ui.edits (v1.2, ADR-91): UNDO, REDO and A | B on the preset strip (views/EditControls.h) over a FakeFacade, whose
// EditHistory is the real one (its ports' gestures and its batches record), driven through HeadlessHost input
// (Panel{skipHint, syncPreview}; settle at 1/60 s; dpi 2, theme 0). Spec-only.
//
//   a11y.*       buttons "Undo" and "Redo" at layout::edits, disabled with nothing to take back; a radioGroup "Compare"
//                of radioButtons "A" (checked) and "B"; they follow SAVE in the Tab order
//   click.*      after one THRESHOLD gesture Undo is enabled and its footer line is "UNDO THRESHOLD   CMD-Z" (CTRL-Z off
//                macOS); a click on UNDO puts the value back, a click on REDO the new one
//   keys.*       Cmd-Z undoes and Shift-Cmd-Z redoes, whatever has the focus; with nothing to take back the host keeps
//                the key (Panel::key false); under the preset browser Cmd-Z takes nothing back; with Ctrl held too it
//                is the host's chord on macOS, and the undo chord itself elsewhere (JUCE's command key is Ctrl there,
//                so a real Ctrl-Z carries both flags, ADR-92)
//   wheel.*      Cmd-Z at once after a wheel burst on THRESHOLD (still open: it closes 0.5 s after its last notch) undoes
//                the burst
//   ab.*         a click on B selects it (and fills it); a click on A comes back; on the focused group → selects B, ←
//                A, Return the other one
//   popup        a ctrl-click on A headless opens nothing and changes nothing
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "editor/views/EditControls.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/params/ParamPort.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace E = fcmp::ui::layout::edits;
    using fcmp::probe::FakeFacade;
    using fcdsp::Pid;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };

    int b(bool v) { return v ? 1 : 0; }
    uint32_t sid(uint32_t local) { return ui::a11yId(ui::ViewIndex::presetStrip, local); }

    struct Rig
    {
        Rig() : facade("clean"), panel(facade, kProbeOptions), host(panel, 0, 2.0f) { host.settle(kMaxSettle, kDt); }
        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        void tick() { host.tick(2, kDt); }
        void click(const funkgui::Rect& r, funkgui::Mods m = {})
        {
            host.click(r.centreX(), r.centreY(), m);
            tick();
        }
        void edit(Pid pid, float plain)                          // one editor gesture
        {
            funkgui::ParamPort& p = facade.port(pid);
            p.beginGesture();
            p.setValue01(fcdsp::toNorm(pid, plain));
            p.endGesture();
            tick();
        }
        float raw(Pid p) { return facade.fakePort(p).plain(); }
        const funkgui::A11yItem* item(uint32_t id)
        {
            items = host.accessibility();
            for (const funkgui::A11yItem& it : items)
                if (it.id == id)
                    return &it;
            return nullptr;
        }

        FakeFacade            facade;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
        std::vector<funkgui::A11yItem> items;
    };

    void a11yRows(Probe& P)
    {
        Rig r;
        const funkgui::A11yItem* u = r.item(sid(ui::EditControls::kUndoLocal));
        P.eq("a11y.undo", b(u != nullptr && u->role == funkgui::A11yRole::button && u->title == "Undo" && !u->enabled), 1);
        const funkgui::A11yItem* rd = r.item(sid(ui::EditControls::kRedoLocal));
        P.eq("a11y.redo", b(rd != nullptr && rd->title == "Redo" && !rd->enabled), 1);
        const funkgui::A11yItem* g = r.item(sid(ui::EditControls::kGroupLocal));
        P.eq("a11y.compare", b(g != nullptr && g->role == funkgui::A11yRole::radioGroup && g->title == "Compare"), 1);
        const funkgui::A11yItem* a = r.item(sid(ui::EditControls::kGroupLocal + 1u));
        P.eq("a11y.a_checked", b(a != nullptr && a->checked), 1);

        // The Tab order: SAVE, then UNDO, REDO, the group.
        std::vector<uint32_t> order;
        for (int i = 0; i < 16; ++i)
        {
            r.host.keys("tab");
            order.push_back(r.panel.context().focus);
        }
        bool after = false;
        for (std::size_t i = 0; i + 3 < order.size(); ++i)
            after = after || (order[i] == sid(4) && order[i + 1] == sid(ui::EditControls::kUndoLocal)
                              && order[i + 2] == sid(ui::EditControls::kRedoLocal)
                              && order[i + 3] == sid(ui::EditControls::kGroupLocal));
        P.eq("a11y.tab_after_save", b(after), 1);
    }

    void clickRows(Probe& P)
    {
        Rig r;
        const float before = r.raw(Pid::thr);
        r.edit(Pid::thr, -27.0f);
        const float after = r.raw(Pid::thr);
        const funkgui::A11yItem* u = r.item(sid(ui::EditControls::kUndoLocal));
        P.eq("click.undo_enabled", b(u != nullptr && u->enabled), 1);
        r.host.move(E::kUndo.centreX(), E::kUndo.centreY());
        r.tick();
        const funkgui::A11yItem* footer = r.item(ui::a11yId(ui::ViewIndex::footer, 1));
        const std::string line = std::string("UNDO THRESHOLD   ") + ui::EditControls::commandKeyName() + "-Z";
        P.eq("click.footer", b(footer != nullptr && footer->value == line), 1);
        r.click(E::kUndo);
        P.eq("click.undo", b(r.raw(Pid::thr) == before), 1);
        r.click(E::kRedo);
        P.eq("click.redo", b(r.raw(Pid::thr) == after), 1);
    }

    void keyRows(Probe& P)
    {
        Rig r;
        const float before = r.raw(Pid::thr);
        r.edit(Pid::thr, -35.0f);
        const float after = r.raw(Pid::thr);
        r.host.keys("cmd+z");
        r.tick();
        P.eq("keys.cmd_z", b(r.raw(Pid::thr) == before), 1);
        r.host.keys("shift+cmd+z");
        r.tick();
        P.eq("keys.shift_cmd_z", b(r.raw(Pid::thr) == after), 1);
    }

    funkgui::KeyEvent undoKey(bool shift)
    {
        funkgui::KeyEvent k;
        k.key = funkgui::Key::character;
        k.ch = U'z';
        k.mods.cmd = true;
        k.mods.shift = shift;
        return k;
    }

    void passRows(Probe& P)
    {
        {
            Rig r;
            P.eq("keys.nothing_passes", b(!r.panel.key(undoKey(false)) && !r.panel.key(undoKey(true))), 1);
        }
        {
            Rig r;
            const float before = r.raw(Pid::thr);
            r.edit(Pid::thr, -29.0f);
            const float after = r.raw(Pid::thr);
            funkgui::KeyEvent k = undoKey(false);
            k.mods.ctrl = true;
            const bool used = r.panel.key(k);
            r.tick();
           #if JUCE_MAC
            const bool hostsChord = true;                        // Ctrl-Cmd-Z is not undo: the host keeps it
           #else
            const bool hostsChord = false;                       // Ctrl is the command key: this is undo
           #endif
            P.eq("keys.ctrl_cmd_z", b(used == !hostsChord && r.raw(Pid::thr) == (hostsChord ? after : before)), 1);
        }
        {
            Rig r;
            r.edit(Pid::thr, -36.0f);
            const float after = r.raw(Pid::thr);
            r.panel.setView({ nullptr, ui::Screen::panel, r.panel.scTab(), ui::Overlay::presetBrowser });
            r.tick();
            r.host.keys("cmd+z");
            r.tick();
            P.eq("keys.overlay_keeps", b(r.raw(Pid::thr) == after && r.facade.edits().canUndo()), 1);
        }
        {
            Rig r;
            const float before = r.raw(Pid::thr);
            funkgui::Rect s{};
            for (const funkgui::A11yItem& it : r.host.accessibility())
                if (it.id == ui::a11yId(ui::ViewIndex::slotGrid, 1))     // THRESHOLD: kSlots[0]
                    s = it.bounds;
            r.host.wheel(s.centreX(), s.centreY(), 1.0f);
            r.host.wheel(s.centreX(), s.centreY(), 1.0f);
            r.tick();
            const bool moved = r.raw(Pid::thr) != before;
            r.host.keys("cmd+z");
            r.tick();
            P.eq("wheel.undo_at_once", b(moved && r.raw(Pid::thr) == before), 1);
        }
    }

    void abRows(Probe& P)
    {
        Rig r;
        fcmp::EditAccess& e = r.facade.edits();
        r.edit(Pid::thr, -30.0f);
        r.click(E::kB);
        P.eq("ab.click_b", b(e.compareSlot() == 1 && e.slotUsed(1)), 1);
        r.edit(Pid::thr, -10.0f);
        r.click(E::kA);
        P.eq("ab.click_a", b(e.compareSlot() == 0 && r.raw(Pid::thr) == fcdsp::legal(Pid::thr, -30.0f)), 1);

        r.panel.a11yAction(sid(ui::EditControls::kGroupLocal), funkgui::A11yAction::focus);
        r.tick();
        r.host.keys("right");
        r.tick();
        P.eq("ab.key_right", b(e.compareSlot() == 1), 1);
        r.host.keys("left");
        r.tick();
        P.eq("ab.key_left", b(e.compareSlot() == 0), 1);
        r.host.keys("return");
        r.tick();
        P.eq("ab.key_return", b(e.compareSlot() == 1), 1);

        const int slot = e.compareSlot();
        const uint32_t rev = e.revision();
        r.click(E::kA, funkgui::Mods{ false, false, false, true });   // ctrl: a popup click
        P.eq("popup.headless_nothing", b(e.compareSlot() == slot && e.revision() == rev), 1);
    }
}

FCMP_PROBE(ui, edits)
{
    (void) C;
    const juce::ScopedJuceInitialiser_GUI juceInit;
    a11yRows(P);
    clickRows(P);
    keyRows(P);
    passRows(P);
    abRows(P);
    return P.finish();
}
