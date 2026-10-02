// FCMP_PROBE layer=ui name=web scope=global timeout=300
//
// ui.web (web Sprint C, ADR-93): the editor over the browser demo's facade. The real Panel (Panel{skipHint,
// syncPreview}; dpi 2, theme 0) over a WebFacade and a LoopbackLink (the engine module in this process: the gate off,
// 48 kHz, 128-frame quanta), driven through HeadlessHost input, with the records each action posts to the engine.
// Every frame is the page's: audio quanta, WebFacade::pull(), then the Panel's tick. Spec-only. No JUCE and no
// Processor in this file, so the node build of Sprint D runs it as it is; the command key's name and the undo chord
// are what the host's commandKeyIsMeta() says, never a platform macro, and the probe never sets it.
//
//   typed.*       Return on the focused THRESHOLD, "-24.5", Return: -24.5 dB in the facade's raw value, one Params
//                 record, no snap (an edit ramps)
//   undo.*        Undo is enabled and its footer line is "UNDO THRESHOLD   <CMD or CTRL>-Z"; a click on UNDO puts the
//                 value back and a click on REDO the new one: one record each, no snap
//   chord.*       the command key and Z undoes, with Shift redoes (off Apple platforms the command key is Ctrl and the
//                 event carries both flags): one record each, no snap
//   ab.*          a click on B selects it (a record although B starts as a copy of A: nothing moved); an edit there; a
//                 click on A brings A's value back: one record each, no snap
//   preset.*      a click on the strip's next arrow is the second preset: one record WITH the snap, the strip shows its
//                 name; undo by click goes back to Init (the identity with it), one record, no snap
//   browser.*     a click on the strip's name opens the browser and posts nothing; a click on a row applies that
//                 preset: one record with the snap
//   telemetry.*   with audio running the Panel has a live frame, the mirror ring fills (the Panel's history with it),
//                 and after the typed value the engine's own threshold (the reply's UiFrame) has followed it
//   link.*        nothing refused either way; one Attach; one Pull a frame
#include "ProbeRegistry.h"

#include "LoopbackLink.h"
#include "Signals.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "editor/views/EditControls.h"
#include "editor/views/PresetBrowser.h"
#include "editor/views/PresetStrip.h"

#include "web/engine/WebEngine.h"
#include "web/facade/WebFacade.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/panel/HeadlessGuiScope.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/Input.h>
#include <funkgui/params/ParamPort.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace L = fcmp::ui::layout;
    namespace E = fcmp::ui::layout::edits;
    namespace sig = fcmp::probe::sig;
    using fcdsp::Pid;

    constexpr double kFs = 48000.0;
    constexpr int    kQuantum = 128;
    constexpr int    kQuantaPerFrame = 6;                        // a 60 Hz frame is 6.25 quanta
    constexpr int    kMaxSettle = 600;
    constexpr float  kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive

    int b(bool v) { return v ? 1 : 0; }
    bool sameBits(float a, float c) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(c); }

    uint32_t stripId(uint32_t local) { return ui::a11yId(ui::ViewIndex::presetStrip, local); }
    uint32_t slotId(Pid pid)                                     // (slotGrid << 16) | (1 + its index in layout::kSlots)
    {
        for (std::size_t i = 0; i < L::kSlots.size(); ++i)
            if (L::kSlots[i].pid == pid)
                return ui::a11yId(ui::ViewIndex::slotGrid, static_cast<uint32_t>(1 + i));
        return 0;
    }

    struct Rig
    {
        Rig()
        {
            fcmp_web_set_gate(link.engine(), 0);
            fcmp_web_configure(link.engine(), kFs, kQuantum);
            facade.setEngineSetup(kFs, kQuantum);
            panel = std::make_unique<ui::Panel>(facade, kProbeOptions);
            host = std::make_unique<funkgui::HeadlessHost>(*panel, 0, 2.0f);
            facade.setUiAttached(true);                          // the editor is open
            host->settle(kMaxSettle, kDt);
            frames(2);
            attachesAtStart = link.log().attaches;
            link.clearLog();
        }
        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        // The page's frame: the audio since the last one, the Pull, the Panel.
        void frames(int n)
        {
            float inL[kQuantum], inR[kQuantum], outL[kQuantum], outR[kQuantum];
            for (int k = 0; k < n; ++k)
            {
                for (int q = 0; q < kQuantaPerFrame; ++q)
                {
                    sig::sine(std::span<float>(inL, kQuantum), 1000.0, kFs, 0.25, 0.0, sample);
                    sig::noise(std::span<float>(inR, kQuantum), rng, 0.25f);
                    sample += kQuantum;
                    fcmp_web_process(link.engine(), inL, inR, outL, outR, kQuantum);
                }
                facade.pull();
                host->tick(1, kDt);
                ++frameCount;
            }
        }

        void click(const funkgui::Rect& r)
        {
            host->click(r.centreX(), r.centreY());
            frames(2);
        }
        void keys(const char* spec)
        {
            host->keys(spec);
            frames(1);
        }
        void focus(uint32_t id)
        {
            panel->a11yAction(id, funkgui::A11yAction::focus);
            frames(1);
        }

        const funkgui::A11yItem* item(uint32_t id)
        {
            items = host->accessibility();
            for (const funkgui::A11yItem& it : items)
                if (it.id == id)
                    return &it;
            return nullptr;
        }
        funkgui::Rect bounds(uint32_t id)
        {
            const funkgui::A11yItem* it = item(id);
            return it != nullptr ? it->bounds : funkgui::Rect{ -100.0f, -100.0f, 1.0f, 1.0f };
        }
        std::string footer()
        {
            const funkgui::A11yItem* it = item(ui::a11yId(ui::ViewIndex::footer, 1));
            return it != nullptr ? it->value : std::string("<none>");
        }

        float raw(Pid p) const { return facade.plain(p); }

        // The Params records since the last call: `records` of them, `snaps` with the snap, the last one carrying the
        // facade's 30 raw values. 1: as expected.
        int posted(int records, int snaps)
        {
            const fcmp::probe::LoopbackLink::Log& log = link.log();
            bool ok = log.params == records && log.snapped() == snaps;
            if (log.params > 0)
                for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
                    ok = ok && sameBits(log.lastParams[i], facade.plain(static_cast<Pid>(i)));
            pulls += log.pulls;
            refused += log.refused;
            attaches += log.attaches;
            link.clearLog();
            return b(ok);
        }

        fcmp::probe::LoopbackLink              link;
        fcmp::web::WebFacade                   facade{ link };
        std::unique_ptr<ui::Panel>             panel;            // destroyed after the host, which closes its gestures
        std::unique_ptr<funkgui::HeadlessHost> host;
        std::vector<funkgui::A11yItem>         items;
        sig::Pcg32   rng{ 0x75697765u };
        std::int64_t sample = 0;
        int          frameCount = 0, pulls = 0, refused = 0, attaches = 0, attachesAtStart = 0;
    };

    // The undo chord as this host's platform sends it: the command key alone on an Apple platform; elsewhere the
    // command key is Ctrl, and the event carries both flags (JUCE's ModifierKeys, FunkGui's WebInput).
    funkgui::KeyEvent undoKey(const funkgui::HostServices& host, bool shift)
    {
        funkgui::KeyEvent k;
        k.key = funkgui::Key::character;
        k.ch = U'z';
        k.mods.cmd = true;
        k.mods.ctrl = !host.commandKeyIsMeta();
        k.mods.shift = shift;
        return k;
    }

    void editRows(Probe& P)
    {
        Rig r;
        const float before = r.raw(Pid::thr);

        // A typed value.
        r.focus(slotId(Pid::thr));
        r.keys("return");
        P.eq("typed.opens", b(r.panel->context().textEntry == static_cast<int>(ui::ViewIndex::slotGrid)), 1);
        P.eq("typed.opens.records", r.posted(0, 0), 1);
        r.keys("-,2,4,.,5,return");
        const float after = r.raw(Pid::thr);
        P.near("typed.db", static_cast<double>(after), -24.5, 1.0e-4);
        P.eq("typed.closed", b(r.panel->context().textEntry < 0), 1);
        P.eq("typed.records", r.posted(1, 0), 1);

        // Undo and redo by click.
        const std::string key = r.host->commandKeyIsMeta() ? "CMD" : "CTRL";
        const funkgui::A11yItem* u = r.item(stripId(ui::EditControls::kUndoLocal));
        P.eq("undo.enabled", b(u != nullptr && u->enabled), 1);
        r.host->move(E::kUndo.centreX(), E::kUndo.centreY());
        r.frames(2);
        P.eq("undo.footer", b(r.footer() == "UNDO THRESHOLD   " + key + "-Z"), 1);
        r.click(E::kUndo);
        P.eq("undo.click", b(sameBits(r.raw(Pid::thr), before)), 1);
        P.eq("undo.click.records", r.posted(1, 0), 1);
        r.host->move(E::kRedo.centreX(), E::kRedo.centreY());
        r.frames(2);
        P.eq("undo.footer_redo", b(r.footer() == "REDO THRESHOLD   SHIFT-" + key + "-Z"), 1);
        r.click(E::kRedo);
        P.eq("undo.redo_click", b(sameBits(r.raw(Pid::thr), after)), 1);
        P.eq("undo.redo_click.records", r.posted(1, 0), 1);

        // By chord.
        const bool usedUndo = r.panel->key(undoKey(*r.host, false));
        r.frames(1);
        P.eq("chord.undo", b(usedUndo && sameBits(r.raw(Pid::thr), before)), 1);
        P.eq("chord.undo.records", r.posted(1, 0), 1);
        const bool usedRedo = r.panel->key(undoKey(*r.host, true));
        r.frames(1);
        P.eq("chord.redo", b(usedRedo && sameBits(r.raw(Pid::thr), after)), 1);
        P.eq("chord.redo.records", r.posted(1, 0), 1);
        const bool usedAgain = r.panel->key(undoKey(*r.host, true));   // nothing to redo: the host keeps the key
        P.eq("chord.nothing_passes", b(!usedAgain), 1);
        P.eq("chord.nothing_passes.records", r.posted(0, 0), 1);

        // Telemetry: the engine ran the edit, and the Panel saw it.
        r.frames(30);                                            // 0.48 s of audio: the 20 ms ramp is long over
        fcdsp::UiFrame frame{};
        P.eq("telemetry.frame", b(r.facade.readUiFrame(frame) && frame.publishCount > 0u), 1);
        P.near("telemetry.thr_follows", static_cast<double>(frame.thrDb), -24.5, 1.0e-3);
        const ui::FrameState& fs = r.panel->context().frame;
        P.eq("telemetry.panel_live", b(fs.hasFrame && fs.fresh && fs.live && fs.ui.publishCount == frame.publishCount),
             1);
        P.ge("telemetry.columns", static_cast<double>(r.facade.history().written()), 400.0);
        P.eq("telemetry.latency", r.facade.latencySamples(), fcmp_web_latency(r.link.engine()));
        r.posted(0, 0);

        // A/B.
        fcmp::EditAccess& e = r.facade.edits();
        r.click(E::kB);
        P.eq("ab.click_b", b(e.compareSlot() == 1 && e.slotUsed(1) && sameBits(r.raw(Pid::thr), after)), 1);
        P.eq("ab.click_b.records", r.posted(1, 0), 1);
        r.focus(slotId(Pid::thr));
        r.keys("return");
        r.keys("-,1,0,return");
        P.near("ab.edit_in_b", static_cast<double>(r.raw(Pid::thr)), -10.0, 1.0e-4);
        P.eq("ab.edit_in_b.records", r.posted(1, 0), 1);
        r.click(E::kA);
        P.eq("ab.click_a", b(e.compareSlot() == 0 && sameBits(r.raw(Pid::thr), after)), 1);
        P.eq("ab.click_a.records", r.posted(1, 0), 1);

        P.eq("link.edits.refused", r.refused + static_cast<int>(r.facade.repliesRefused()), 0);
        P.eq("link.edits.one_pull_a_frame", b(r.pulls == r.frameCount - 2), 1);
        P.eq("link.edits.one_attach", b(r.attachesAtStart == 1 && r.attaches == 0), 1);
    }

    void presetRows(Probe& P)
    {
        using PB = ui::PresetBrowser;
        using PS = ui::PresetStrip;
        Rig r;
        fcmp::PresetAccess& pr = r.facade.presets();
        P.eq("preset.fresh", b(pr.current() == 0 && !pr.modified() && pr.count() > 4), 1);

        // The strip's next arrow.
        r.click(r.bounds(stripId(PS::kNextLocal)));
        P.eq("preset.next", b(pr.current() == 1 && !pr.modified()), 1);
        P.eq("preset.next.records", r.posted(1, 1), 1);
        const funkgui::A11yItem* name = r.item(stripId(PS::kNameLocal));
        P.eq("preset.next.shown", b(name != nullptr && name->value.find(pr.row(1).name) != std::string::npos), 1);

        // Undo by click: the values and the identity.
        r.click(E::kUndo);
        P.eq("preset.undo", b(pr.current() == 0 && !pr.modified()), 1);
        P.eq("preset.undo.records", r.posted(1, 0), 1);

        // The browser: opening it applies nothing; a click on a row applies that preset.
        r.click(r.bounds(stripId(PS::kNameLocal)));
        r.frames(30);                                            // the browser fades in
        P.eq("browser.opens", b(r.panel->overlay() == ui::Overlay::presetBrowser && pr.current() == 0), 1);
        P.eq("browser.opens.records", r.posted(0, 0), 1);
        std::vector<funkgui::A11yItem> rows;
        for (const funkgui::A11yItem& it : r.host->accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::presetBrowser)
                && it.role == funkgui::A11yRole::listItem && it.visible)
                rows.push_back(it);
        std::stable_sort(rows.begin(), rows.end(), [](const funkgui::A11yItem& a, const funkgui::A11yItem& c) {
            return a.bounds.y < c.bounds.y;
        });
        P.ge("browser.rows_shown", static_cast<double>(rows.size()), 5.0);
        if (rows.size() >= 5)
        {
            const funkgui::A11yItem& row = rows[4];
            const int index = static_cast<int>((row.id & 0xffffu) - PB::kRowLocal0);
            r.click(row.bounds);
            P.eq("browser.row_applies", b(index > 0 && pr.current() == index && !pr.modified()
                                          && pr.row(index).name == row.title), 1);
            P.eq("browser.row_applies.records", r.posted(1, 1), 1);
            P.eq("browser.stays_open", b(r.panel->overlay() == ui::Overlay::presetBrowser), 1);
        }
        P.eq("link.presets.refused", r.refused + static_cast<int>(r.facade.repliesRefused()), 0);
    }
} // namespace

FCMP_PROBE(ui, web)
{
    (void) C;
    const funkgui::HeadlessGuiScope gui;
    editRows(P);
    presetRows(P);
    return P.finish();
}
