// FCMP_PROBE layer=ui name=a11y scope=mode timeout=300
//
// ui.a11y.<key> (03 §3.6, C's G4; 02 §8.9; U1s): the accessibility model of fcmp::ui::Panel, per Mode, as text.
//
// Golden rows (candidates only until FZ5, like ui.geometry's: never blessed before S13):
//   lines <view>        every visible item of the view in a11yDumpLine format, sidecar
//                       modes/<key>/ui.a11y.<view>.lines (03 §3.2.2), for each view of fcmp::ui::views()
// Spec rows, on the panel view (02 §8.9's table, per the Mode's resolved slot states):
//   a11y.<pid>.contract   continuous: a slider over the Mode track in display units (lo < hi, lo <= v <= hi), writable;
//                         stepped: a slider in index space {0, n − 1}, step 1, v = the active detent, help "n steps: …";
//                         locked: a read-only, disabled slider with help (the reason); derived: read-only, enabled,
//                         with help; n/a: disabled static text with help. Title = the Mode's label; a renamed slot's
//                         description is "controls <universal name>".
//   a11y.slots            21 slot items, visible, in slot order
//   a11y.word.<word>      AUTO / EXT / LISTEN: a checkable toggleButton, checked = the switch, AUTO present exactly
//                         when automu applies to the Mode (hidden when n/a; disabled with its reason when locked)
//   a11y.word.ext_no_bus  EXT with the key on and fresh telemetry without an active key bus carries the help
//                         NO SIDECHAIN BUS CONNECTED and stays enabled; with the bus, no such help
//   a11y.<view>.ids       every item id non-zero and unique within the view
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SlotModel.h"
#include "editor/SubView.h"
#include "editor/views/SlotGrid.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/text/FontService.h>
#include <funkgui/widgets/ValueModel.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    namespace ui = fcmp::ui;
    namespace layout = fcmp::ui::layout;
    using funkgui::test::Probe;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;

    std::string pidName(fcdsp::Pid p) { return fcdsp::kHostParams[fcdsp::idx(p)].id; }

    std::string lowerAscii(const char* s)
    {
        std::string out = s != nullptr ? s : "";
        for (char& c : out)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return out;
    }

    const funkgui::A11yItem* find(const std::vector<funkgui::A11yItem>& items, uint32_t id)
    {
        for (const funkgui::A11yItem& it : items)
            if (it.id == id && it.visible)
                return &it;
        return nullptr;
    }

    // One Mode's panel, settled, over a FakeFacade.
    struct Rig
    {
        explicit Rig(std::string_view key) : facade(key), panel(facade, { true, true, false }), host(panel, 0, 2.0f) {}
        fcmp::probe::FakeFacade facade;
        ui::Panel               panel;
        funkgui::HeadlessHost   host;
    };

    // ---- the per-slot contract (02 §8.9) -------------------------------------------------------------------------------

    bool contract(const funkgui::A11yItem& it, const funkgui::ValueView& v, std::string& why)
    {
        using funkgui::A11yRole;
        const auto fail = [&](const char* w) { why = w; return false; };
        const std::string title = v.label != nullptr ? v.label : "";
        switch (v.state)
        {
            case funkgui::ValueState::continuous:
                if (it.role != A11yRole::slider) return fail("not a slider");
                if (it.readOnly || !it.enabled) return fail("not writable");
                if (!(it.lo < it.hi)) return fail("empty display range");
                if (it.v < it.lo || it.v > it.hi) return fail("value outside its range");
                if (!(it.step > 0.0)) return fail("no step");
                break;
            case funkgui::ValueState::stepped:
            {
                if (it.role != A11yRole::slider) return fail("not a slider");
                if (it.readOnly || !it.enabled) return fail("not writable");
                if (it.lo != 0.0 || it.hi != static_cast<double>(v.nDetents - 1) || it.step != 1.0)
                    return fail("not in index space {0, n-1} step 1");
                const int active = v.detent >= 0 ? v.detent : 0;
                if (it.v != static_cast<double>(active)) return fail("v is not the active detent");
                if (it.help.rfind(std::to_string(v.nDetents) + " steps: ", 0) != 0) return fail("help does not list the steps");
                break;
            }
            case funkgui::ValueState::locked:
                if (it.role != A11yRole::slider) return fail("not a slider");
                if (!it.readOnly || it.enabled) return fail("not read-only and disabled");
                if (it.help.empty()) return fail("no help (reason)");
                break;
            case funkgui::ValueState::derived:
                if (it.role != A11yRole::slider) return fail("not a slider");
                if (!it.readOnly || !it.enabled) return fail("not read-only and enabled");
                if (it.help.empty()) return fail("no help (reason)");
                break;
            case funkgui::ValueState::na:
                if (it.role != A11yRole::staticText) return fail("not static text");
                if (it.enabled) return fail("not disabled");
                if (it.help.empty()) return fail("no help (reason)");
                break;
        }
        const std::string wantTitle = v.state == funkgui::ValueState::na ? title + ", not applicable" : title;
        if (it.title != wantTitle)
            return fail("title is not the Mode's label");
        if (v.aka != nullptr && it.description != "controls " + lowerAscii(v.aka))
            return fail("a renamed slot does not say what it controls");
        return true;
    }

    void panelContract(Probe& P, const fcdsp::ModeEntry& entry)
    {
        Rig r(entry.desc->key);
        if (r.host.settle(kMaxSettle, kDt) > kMaxSettle)
        {
            P.harnessError("ui.a11y: the panel did not settle within 600 frames");
            return;
        }
        const std::vector<funkgui::A11yItem> items = r.host.accessibility();
        int slots = 0;
        for (std::size_t i = 0; i < layout::kSlots.size(); ++i)
        {
            const layout::SlotPlace& place = layout::kSlots[i];
            const funkgui::A11yItem* it = find(items, ui::a11yId(ui::ViewIndex::slotGrid, static_cast<uint32_t>(1 + i)));
            funkgui::ValueView v;
            r.panel.context().slot(place.pid).view(v);
            std::string why = "missing";
            const bool ok = it != nullptr && contract(*it, v, why);
            if (!ok)
                std::printf("NOTE     a11y.%s: %s\n", pidName(place.pid).c_str(), why.c_str());
            P.eq("a11y." + pidName(place.pid) + ".contract", ok ? 1 : 0, 1);
            slots += it != nullptr ? 1 : 0;

            if (place.word == fcdsp::kNoPid)
                continue;
            // The word follows its slot (02 §6.4, K1 #23).
            const uint32_t wid = ui::a11yId(ui::ViewIndex::slotGrid, ui::SlotGrid::kWordIdBase + static_cast<uint32_t>(i));
            const funkgui::A11yItem* w = find(items, wid);
            const bool modeFiltered = fcdsp::idx(place.word) < fcdsp::kNumModeParams;
            const fcdsp::ResolvedParam* rp = modeFiltered ? &r.panel.context().frame.res.view.p[fcdsp::idx(place.word)]
                                                          : nullptr;
            const bool shown = rp == nullptr || rp->state != fcdsp::SlotState::na;
            const bool on = rp != nullptr ? rp->plain >= 0.5f : r.facade.port(place.word).value01() >= 0.5f;
            bool wok = (w != nullptr) == shown;
            if (w != nullptr)
            {
                wok = wok && w->role == funkgui::A11yRole::toggleButton && w->checkable && w->checked == on
                   && w->title == place.wordLabel;
                if (rp != nullptr && rp->state == fcdsp::SlotState::locked)
                    wok = wok && !w->enabled && !w->help.empty();
                else
                    wok = wok && w->enabled;
            }
            P.eq("a11y.word." + lowerAscii(place.wordLabel), wok ? 1 : 0, 1);
        }
        P.eq("a11y.slots", slots, 21);

        // EXT without a key bus (02 §6.4): drawn disabled with the reason, still operable.
        const std::size_t detIndex = static_cast<std::size_t>(layout::slotOf(fcdsp::Pid::det) - layout::kSlots.data());
        const uint32_t extId = ui::a11yId(ui::ViewIndex::slotGrid, ui::SlotGrid::kWordIdBase + static_cast<uint32_t>(detIndex));
        r.facade.setPlain(fcdsp::Pid::extkey, 1.0f);
        fcdsp::UiFrame f = fcmp::probe::FakeFacade::quietFrame(r.panel.context().frame.res.view.slot,
                                                               r.panel.context().frame.res.eng);
        r.facade.publish(f);                                      // no kUiExtKeyActive: no key bus
        r.host.tick(1, kDt);
        const std::vector<funkgui::A11yItem> noBusItems = r.host.accessibility();
        const funkgui::A11yItem* noBus = find(noBusItems, extId);
        const bool noBusOk = noBus != nullptr && noBus->enabled && noBus->checked
                          && noBus->help == "NO SIDECHAIN BUS CONNECTED";
        f.flags |= fcdsp::kUiExtKeyActive;
        r.facade.publish(f);
        r.host.tick(1, kDt);
        if (!noBusOk)
            std::printf("NOTE     EXT without a bus: %s\n",
                        noBus != nullptr ? funkgui::a11yDumpLine(*noBus).c_str() : "no item");
        const std::vector<funkgui::A11yItem> busItems = r.host.accessibility();
        const funkgui::A11yItem* bus = find(busItems, extId);
        const bool busOk = bus != nullptr && bus->enabled && bus->help.empty();
        if (!busOk)
            std::printf("NOTE     EXT with a bus: %s\n", bus != nullptr ? funkgui::a11yDumpLine(*bus).c_str() : "no item");
        P.eq("a11y.word.ext_no_bus", noBusOk && busOk ? 1 : 0, 1);
    }

    // ---- the lines of every view ----------------------------------------------------------------------------------------

    void viewLines(Probe& P, const fcdsp::ModeEntry& entry)
    {
        for (const ui::ViewSpec& view : ui::views())
        {
            Rig r(entry.desc->key);
            r.panel.setView(view, true);
            if (r.host.settle(kMaxSettle, kDt) > kMaxSettle)
            {
                P.harnessError(std::string("ui.a11y: view ") + view.id + " did not settle within 600 frames");
                continue;
            }
            std::vector<std::string> lines;
            std::set<uint32_t> ids;
            bool unique = true;
            for (const funkgui::A11yItem& it : r.host.accessibility())
            {
                if (!it.visible)
                    continue;                                     // not in the tree an assistive technology sees
                unique = unique && it.id != 0 && ids.insert(it.id).second;
                lines.push_back(funkgui::a11yDumpLine(it));
            }
            P.eq(std::string("a11y.") + view.id + ".ids", unique ? 1 : 0, 1);
            P.ge(std::string("a11y.") + view.id + ".items", static_cast<double>(lines.size()), 1.0);
            P.lines(view.id, lines);
        }
    }
}

FCMP_PROBE(ui, a11y)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.a11y: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    P.eq("a11y.font.ok", funkgui::FontService::get().atlas().baked() ? 1 : 0, 1);
    panelContract(P, *entry);
    viewLines(P, *entry);
    return P.finish();
}
