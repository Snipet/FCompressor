// FCMP_PROBE layer=ui name=edits scope=global timeout=60
//
// ui.edits (v1.2, ADR-91): UNDO, REDO and A | B on the preset strip (views/EditControls.h) over a FakeFacade, whose
// EditHistory is the real one (its ports' gestures and its batches record), driven through HeadlessHost input
// (Panel{skipHint, syncPreview}; settle at 1/60 s; dpi 2, theme 0). Spec-only. The command key is the host's
// (HostServices::commandKeyIsMeta(), web Sprint C): the rows that name it read HeadlessHost's answer, which is the
// platform's until keys.meta_* set it.
//
//   a11y.*       buttons "Undo" and "Redo" at layout::edits, disabled with nothing to take back; a radioGroup "Compare"
//                of radioButtons "A" (checked) and "B"; they follow SAVE in the Tab order
//   click.*      after one THRESHOLD gesture Undo is enabled and its footer line is "UNDO THRESHOLD   CMD-Z" (CTRL-Z off
//                macOS); a click on UNDO puts the value back and REDO's line is "REDO THRESHOLD   SHIFT-CMD-Z" (or
//                SHIFT-CTRL-Z); a click on REDO puts the new value back
//   keys.*       Cmd-Z undoes and Shift-Cmd-Z redoes, whatever has the focus; with nothing to take back the host keeps
//                the key (Panel::key false); under the preset browser Cmd-Z takes nothing back; with Ctrl held too it
//                is the host's chord on macOS, and the undo chord itself elsewhere (JUCE's command key is Ctrl there,
//                so a real Ctrl-Z carries both flags, ADR-92)
//   keys.meta_on.*, keys.meta_off.*   both answers of the host on one platform (HeadlessHost::setCommandKeyIsMeta):
//                the footer lines name CMD or CTRL; the chord with the command flag alone undoes and redoes under
//                both; Ctrl-Cmd-Z is the host's under Meta and the undo chord under Ctrl
//   wheel.*      Cmd-Z at once after a wheel burst on THRESHOLD (still open: it closes 0.5 s after its last notch) undoes
//                the burst
//   ab.*         a click on B selects it (and fills it); a click on A comes back; on the focused group → selects B, ←
//                A, Return the other one
//   popup        a ctrl-click on A asks the host for a menu and, left unanswered, changes nothing
//   menu.*       (web Sprint C, ADR-93) A | B's menu through HostServices::showMenu, which HeadlessHost keeps pending
//                until the probe answers: the request in PAPER at a 150 % UI zoom is one item (2, "Copy A to B"),
//                anchored on the two letters in the Panel's own px (the host scales), in the product's palette;
//                choosing it fills B with A's sound and keeps A selected; on B (a11y showMenu) the item is (1, "Copy B
//                to A") and choosing it copies back; a dismissed menu changes nothing; an EditControls that went while
//                its menu was open is not reached by the answer
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/ProductTheme.h"
#include "editor/SubView.h"
#include "editor/views/EditControls.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Theme.h>
#include <funkgui/panel/HeadlessGuiScope.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/panel/Input.h>
#include <funkgui/params/ParamPort.h>

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
        explicit Rig(int theme = 0) : facade("clean"), panel(facade, kProbeOptions), host(panel, theme, 2.0f)
        {
            host.settle(kMaxSettle, kDt);
        }
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
        // The footer names the host's command key, spelled out here (not read back from EditControls).
        const bool meta = r.host.commandKeyIsMeta();
        const char* undoLine = meta ? "UNDO THRESHOLD   CMD-Z" : "UNDO THRESHOLD   CTRL-Z";
        const char* redoLine = meta ? "REDO THRESHOLD   SHIFT-CMD-Z" : "REDO THRESHOLD   SHIFT-CTRL-Z";
        const funkgui::A11yItem* footer = r.item(ui::a11yId(ui::ViewIndex::footer, 1));
        P.eq("click.footer", b(footer != nullptr && footer->value == undoLine), 1);
        r.click(E::kUndo);
        P.eq("click.undo", b(r.raw(Pid::thr) == before), 1);
        r.host.move(E::kRedo.centreX(), E::kRedo.centreY());
        r.tick();
        const funkgui::A11yItem* footerRedo = r.item(ui::a11yId(ui::ViewIndex::footer, 1));
        P.eq("click.footer_redo", b(footerRedo != nullptr && footerRedo->value == redoLine), 1);
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
            // Meta is the command key: Ctrl-Cmd-Z is not undo, the host keeps it. Ctrl is: this is undo.
            const bool hostsChord = r.host.commandKeyIsMeta();
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

    // ---- the command key is the host's (web Sprint C, ADR-93) -------------------------------------------------------

    std::string footerLine(Rig& r)
    {
        const funkgui::A11yItem* footer = r.item(ui::a11yId(ui::ViewIndex::footer, 1));
        return footer != nullptr ? footer->value : std::string("<none>");
    }

    void metaRows(Probe& P)
    {
        for (const bool meta : { true, false })
        {
            const std::string key = meta ? "keys.meta_on." : "keys.meta_off.";
            Rig r;
            r.host.setCommandKeyIsMeta(meta);
            const float before = r.raw(Pid::thr);
            r.edit(Pid::thr, -27.0f);
            const float after = r.raw(Pid::thr);
            r.host.move(E::kUndo.centreX(), E::kUndo.centreY());
            r.tick();
            P.eq(key + "footer", b(footerLine(r) == (meta ? "UNDO THRESHOLD   CMD-Z" : "UNDO THRESHOLD   CTRL-Z")), 1);
            // The command flag alone is the chord under both (a Mac's key event, or one a test makes).
            const bool undone = r.panel.key(undoKey(false));
            r.tick();
            P.eq(key + "cmd_z", b(undone && before != after && r.raw(Pid::thr) == before), 1);
            r.host.move(E::kRedo.centreX(), E::kRedo.centreY());
            r.tick();
            P.eq(key + "footer_redo",
                 b(footerLine(r) == (meta ? "REDO THRESHOLD   SHIFT-CMD-Z" : "REDO THRESHOLD   SHIFT-CTRL-Z")), 1);
            const bool redone = r.panel.key(undoKey(true));
            r.tick();
            // Ctrl held too: the host's own chord where Meta is the command key (not used, nothing taken back); where
            // Ctrl is, it is what a real Ctrl-Z carries, and undoes.
            funkgui::KeyEvent k = undoKey(false);
            k.mods.ctrl = true;
            const bool used = r.panel.key(k);
            r.tick();
            P.eq(key + "ctrl_cmd_z", b(redone && used == !meta && r.raw(Pid::thr) == (meta ? after : before)), 1);
        }
    }

    // ---- A | B's menu through the host's services (web Sprint C, ADR-93) --------------------------------------------

    funkgui::Mods popupMods()
    {
        funkgui::Mods m;
        m.ctrl = true;                                           // a ctrl-click is a popup click (HeadlessHost)
        return m;
    }

    bool sameCol(funkgui::Col a, funkgui::Col c) { return a.r == c.r && a.g == c.g && a.b == c.b && a.a == c.a; }

    bool sameTheme(const funkgui::Theme& a, const funkgui::Theme& c)
    {
        return sameCol(a.ground, c.ground) && sameCol(a.ink100, c.ink100) && sameCol(a.ink70, c.ink70)
            && sameCol(a.ink52, c.ink52) && sameCol(a.ink32, c.ink32) && sameCol(a.ink16, c.ink16)
            && sameCol(a.accent, c.accent) && sameCol(a.accentDim, c.accentDim) && sameCol(a.signal, c.signal)
            && a.textGamma == c.textGamma;
    }

    void menuRows(Probe& P)
    {
        {
            // The request, in PAPER at a 150 % UI zoom: one item; the anchor is the two letters in the Panel's own px
            // whatever the zoom (the host scales it); the palette is the product's PAPER (ProductTheme.h: ink100
            // 0B0C0E), not FunkGui's and not the request's default GRAPHITE.
            Rig r(ui::kThemePaper);
            r.host.setZoom({ 100, 150 }, 150);
            r.click(E::kA, popupMods());
            const funkgui::MenuRequest* m = r.host.pendingMenu();
            P.eq("menu.copy_request", b(m != nullptr && r.host.log.menuRequests == 1 && m->items.size() == 1
                                        && m->items[0].id == 2 && m->items[0].label == "Copy A to B"
                                        && m->items[0].enabled && !m->items[0].checked && !m->items[0].separator), 1);
            P.eq("menu.copy_anchor", b(m != nullptr && m->anchor.x == 562.0f && m->anchor.y == 43.0f
                                       && m->anchor.w == 38.0f && m->anchor.h == 16.0f), 1);
            P.eq("menu.copy_theme", b(m != nullptr && sameTheme(m->theme, ui::productTheme(ui::kThemePaper))
                                      && sameCol(m->theme.ink100, funkgui::Col{ 0x0B, 0x0C, 0x0E })), 1);
        }
        {
            // Choosing it copies the live sound into B: B is used, A stays selected, and B holds what A held then.
            Rig r;
            fcmp::EditAccess& e = r.facade.edits();
            r.edit(Pid::thr, -31.0f);
            const float a = r.raw(Pid::thr);
            r.click(E::kA, popupMods());
            const bool before = !e.slotUsed(1) && r.host.pendingMenu() != nullptr;
            const int nudges = r.host.log.nudges;
            const bool chosen = r.host.chooseMenuItem("Copy A to B");
            r.tick();
            const bool copied = e.slotUsed(1) && e.compareSlot() == 0 && r.host.pendingMenu() == nullptr
                             && r.host.log.nudges > nudges && r.raw(Pid::thr) == a;
            r.edit(Pid::thr, -12.0f);                            // A moves on; B keeps the copy
            r.click(E::kB);
            P.eq("menu.copy_a_to_b", b(before && chosen && copied && e.compareSlot() == 1 && r.raw(Pid::thr) == a), 1);

            // On B the item is "Copy B to A" (id 1), here from a11y showMenu on the group; choosing it copies back.
            r.edit(Pid::thr, -20.0f);
            const float onB = r.raw(Pid::thr);
            r.panel.a11yAction(sid(ui::EditControls::kGroupLocal), funkgui::A11yAction::showMenu);
            const funkgui::MenuRequest* m = r.host.pendingMenu();
            const bool item = m != nullptr && m->items.size() == 1 && m->items[0].id == 1
                           && m->items[0].label == "Copy B to A";
            const bool back = r.host.chooseMenuItem(1);
            r.tick();
            r.click(E::kA);
            P.eq("menu.copy_b_to_a", b(item && back && onB != a && e.compareSlot() == 0 && r.raw(Pid::thr) == onB), 1);
        }
        {
            // Dismissed: nothing is copied and nothing else changes.
            Rig r;
            fcmp::EditAccess& e = r.facade.edits();
            r.click(E::kA, popupMods());
            const uint32_t rev = e.revision();
            const bool pending = r.host.pendingMenu() != nullptr;
            const bool cancelled = r.host.cancelMenu();
            r.tick();
            P.eq("menu.cancel", b(pending && cancelled && e.revision() == rev && !e.slotUsed(1)
                                  && r.host.pendingMenu() == nullptr), 1);
        }
        {
            // An EditControls of the probe's own that goes while its menu is open: the host still holds the callback,
            // and the answer reaches nobody (the weak `alive` guard; no service is called from the destructor).
            Rig r;
            fcmp::EditAccess& e = r.facade.edits();
            const int dismissals = r.host.log.menuDismissals;
            {
                ui::EditControls own(const_cast<ui::PanelContext&>(r.panel.context()));
                funkgui::PointerEvent down;
                down.x = E::kA.centreX();
                down.y = E::kA.centreY();
                down.popup = true;
                own.pointerDown(down);
            }
            const uint32_t rev = e.revision();
            const bool pending = r.host.pendingMenu() != nullptr && r.host.log.menuDismissals == dismissals;
            const bool answered = r.host.chooseMenuItem(2);
            r.tick();
            P.eq("menu.view_gone", b(pending && answered && e.revision() == rev && !e.slotUsed(1)), 1);
        }
    }
}

FCMP_PROBE(ui, edits)
{
    (void) C;
    const funkgui::HeadlessGuiScope gui;
    a11yRows(P);
    clickRows(P);
    keyRows(P);
    passRows(P);
    abRows(P);
    metaRows(P);
    menuRows(P);
    return P.finish();
}
