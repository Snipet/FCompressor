// FCMP_PROBE layer=ui name=input scope=mode timeout=300
//
// ui.input.<key> (03 §3.6, C's G5; 02 §8.4, §8.9; K2 #4, #7, #23; U1s): key, pointer, wheel and a11y replay through
// the Panel API (a FakeFacade, Panel{skipHint, syncPreview}, HeadlessHost at dpi 2), per Mode and per slot, on a fresh
// panel for every scenario so no scenario sees another's writes.
//
// Golden rows (candidates only until FZ5): lines taborder — the Tab order of the panel view, one stop per line
// ("<n> <sub-view>:<local> <role> <title>"), sidecar modes/<key>/ui.input.taborder.lines; and (S13 H1a) the same for
// every other view of fcmp::ui::views(), lines taborder.<view> (the Characteristics screen's two tabs, and each open
// browser's own Tab order), sidecars modes/<key>/ui.input.taborder.<view>.lines.
// Spec rows:
//   input.taborder.{wraps,unique,slots}   Tab cycles; no stop twice; the slot grid's stops are P0…P6, B0…B6, C0…C6,
//                                          each followed by its visible word (02 §8.9 item 6), locked / n/a included
//   stepped slot <pid>:
//     input.<pid>.arrows     up / down / end / home move exactly one detent (or to the end), each writing the detent's
//                            canonical host01 bit for bit (toNorm(pid, step.plain), 02 §8.4.2), nothing at an end
//     input.<pid>.wheel      one notch = one detent, canonical values only
//     input.<pid>.drag       a drag writes canonical detents only, inside one gesture
//     input.<pid>.labels     a click on a drawn detent label writes that detent (only where labels are drawn)
//     input.<pid>.a11y_set   a11y setValue(i) in index space writes detent i
//   continuous slot <pid>:
//     input.<pid>.keys       home / up / shift+up / pageup / end / delete write host01FromTrack of the stepped track
//                            position, or the Mode default (02 §8.9), bit for bit
//     input.<pid>.dblclick   double-click writes the Mode default (ParamSpec::defaultPlain, 02 §8.4.6)
//     input.<pid>.drag       one gesture from down to up
//     input.<pid>.a11y_set   a11y setValue in display units reaches both ends of the Mode track
//   locked / derived / n/a slot <pid>: input.<pid>.refused — click, drag, wheel, keys, double-click and every a11y
//                            action write nothing at all (02 §8.4.5)
//   input.popup              a ctrl-click opens the host menu and writes nothing
//   input.word.{auto,ext,listen}  a click toggles the switch with one tap (AUTO: only where it applies and is not
//                            locked; a hidden AUTO leaves automu alone), Return on the focused word toggles it back
//   input.mode_switch.{only_mode,landing}  a Mode change through the header latch's path is one tap of `mode` and the
//                            landing that follows writes nothing (K2 #4); a host's Mode change writes nothing either
//   input.batch.defaults     "switch + load Mode defaults" (02 §8.4.4) is one tapMany: every write inside one facade
//                            batch and one host batch (K2 #23), each inside its own gesture
//   input.keys.handled       (S13 H1a) on every view of fcmp::ui::views(), every Tab stop (focused, its ring shown) takes
//                            the keys 02 §8.9 gives its kind — Panel::key returns true for each, so a host (Logic, Live)
//                            never also acts on them: slots and handles (locked, derived and n/a included) every value
//                            key, groups the arrows, Home / End, Return / Space, latches, words and buttons Return /
//                            Space, the Mode latch its arrows, Home / End, Return / Space, browser rows their arrows,
//                            Home / End, Return; every stop Tab, Shift-Tab and Esc. NOTE lines name any miss.
//   input.writes.outside_gesture  no write of the whole probe happened outside a begin/end gesture (K2 #7)
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

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Ease.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/panel/Input.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/params/ParamPort.h>
#include <funkgui/prefs/UiPreferences.h>
#include <funkgui/text/FontService.h>
#include <funkgui/widgets/AttachedWord.h>
#include <funkgui/widgets/RuleSlider.h>
#include <funkgui/widgets/ValueModel.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    namespace ui = fcmp::ui;
    namespace layout = fcmp::ui::layout;
    using funkgui::test::Probe;
    using fcmp::probe::FakeWrite;

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

    const char* roleName(funkgui::A11yRole r)
    {
        switch (r)
        {
            case funkgui::A11yRole::slider:       return "slider";
            case funkgui::A11yRole::toggleButton: return "toggleButton";
            case funkgui::A11yRole::button:       return "button";
            case funkgui::A11yRole::radioGroup:   return "radioGroup";
            case funkgui::A11yRole::radioButton:  return "radioButton";
            case funkgui::A11yRole::comboBox:     return "comboBox";
            case funkgui::A11yRole::listItem:     return "listItem";
            case funkgui::A11yRole::staticText:   return "staticText";
            case funkgui::A11yRole::image:        return "image";
            case funkgui::A11yRole::progressBar:  return "progressBar";
        }
        return "?";
    }

    constexpr std::array<const char*, ui::kSubViewCount> kViewNames { "header", "displayRow", "slotGrid", "band",
                                                                        "charScreen", "modeBrowser", "presetStrip",
                                                                        "presetBrowser", "footer" };

    uint32_t slotId(std::size_t i) { return ui::a11yId(ui::ViewIndex::slotGrid, static_cast<uint32_t>(1 + i)); }
    uint32_t wordId(std::size_t i)
    {
        return ui::a11yId(ui::ViewIndex::slotGrid, ui::SlotGrid::kWordIdBase + static_cast<uint32_t>(i));
    }

    std::size_t slotIndex(fcdsp::Pid p)
    {
        return static_cast<std::size_t>(layout::slotOf(p) - layout::kSlots.data());
    }

    // One Mode's panel, settled, its facade's counters cleared.
    struct Rig
    {
        explicit Rig(std::string_view key) : facade(key), panel(facade, { true, true, false }), host(panel, 0, 2.0f)
        {
            settled = host.settle(kMaxSettle, kDt) <= kMaxSettle;
            facade.resetCounts();
        }
        fcmp::probe::FakeFacade facade;
        ui::Panel               panel;
        funkgui::HeadlessHost   host;
        bool                    settled = false;

        funkgui::ValueView view(fcdsp::Pid p) const
        {
            funkgui::ValueView v;
            panel.context().slot(p).view(v);
            return v;
        }
        const ui::SlotModel& model(fcdsp::Pid p) const { return panel.context().slot(p); }
        funkgui::GestureController& gestures() const { return *panel.context().gestures; }
        std::size_t writes() const { return facade.writes().size(); }
        std::span<const FakeWrite> since(std::size_t n) const { return facade.writes().subspan(n); }
        void focus(uint32_t id) { panel.a11yAction(id, funkgui::A11yAction::focus, 0.0); }
        void press(const char* keys)
        {
            host.keys(keys);
            host.tick(1, kDt);                                   // the Panel re-resolves: the next view is current
        }
    };

    // Every write of every rig, for input.writes.outside_gesture.
    struct Tally
    {
        int writes = 0, outside = 0;
        void take(const Rig& r)
        {
            for (const FakeWrite& w : r.facade.writes())
            {
                ++writes;
                outside += w.inGesture ? 0 : 1;
            }
        }
    };

    int activeIndex(const funkgui::ValueView& v)                  // RuleSlider::activeDetent
    {
        const int n = v.nDetents;
        if (n <= 0)
            return -1;
        if (v.detent >= 0 && v.detent < n)
            return v.detent;
        const int i = static_cast<int>(std::floor(std::clamp(v.track, 0.0f, 1.0f) * static_cast<float>(n)));
        return i >= n ? n - 1 : i;
    }

    bool isDetent(const funkgui::ValueView& v, float host01)
    {
        for (int i = 0; i < v.nDetents; ++i)
            if (funkgui::ease::sameBits(v.detents[i].host01, host01))
                return true;
        return false;
    }

    bool labelsDrawn(const funkgui::ValueView& v)
    {
        return funkgui::RuleSlider::detentLabelsFit(funkgui::FontService::get().atlas(), v.detents, v.nDetents,
                                                    layout::kSlotW);
    }

    // ---- stepped slots ------------------------------------------------------------------------------------------------

    // One key on a focused stepped slot: exactly one detent (or none at an end), its canonical value, in a gesture.
    bool steppedKey(Rig& r, fcdsp::Pid pid, const char* key, int want)
    {
        const funkgui::ValueView before = r.view(pid);
        const int from = activeIndex(before);
        const std::size_t w0 = r.writes();
        r.press(key);
        const std::span<const FakeWrite> ws = r.since(w0);
        bool ok = activeIndex(r.view(pid)) == want;
        if (want == from)
            ok = ok && ws.empty();
        else
            ok = ok && ws.size() == 1 && ws[0].pid == pid && ws[0].inGesture
              && funkgui::ease::sameBits(ws[0].value01, before.detents[want].host01);
        return ok;
    }

    void steppedSlot(Probe& P, std::string_view key, std::size_t i, Tally& t)
    {
        const fcdsp::Pid pid = layout::kSlots[i].pid;
        const std::string name = "input." + pidName(pid);
        const funkgui::SlotGeom g = layout::slotGeom(layout::kSlots[i]);
        const float cx = g.x + 0.5f * g.w, ty = g.trackY();

        {   // arrows, end, home
            Rig r(key);
            r.focus(slotId(i));
            const int n = r.view(pid).nDetents;
            bool ok = n > 0;
            for (int k = 0; ok && k < n; ++k)
                ok = steppedKey(r, pid, "up", std::min(activeIndex(r.view(pid)) + 1, n - 1));
            for (int k = 0; ok && k < n; ++k)
                ok = steppedKey(r, pid, "down", std::max(activeIndex(r.view(pid)) - 1, 0));
            ok = ok && steppedKey(r, pid, "end", n - 1) && steppedKey(r, pid, "home", 0);
            P.eq(name + ".arrows", ok ? 1 : 0, 1);
            t.take(r);
        }
        {   // wheel: one notch, one detent
            Rig r(key);
            const int n = r.view(pid).nDetents;
            bool ok = true;
            for (const float dy : { 1.0f, -1.0f })
                for (int k = 0; k < n; ++k)
                {
                    const funkgui::ValueView before = r.view(pid);
                    const int from = activeIndex(before);
                    const int want = std::clamp(from + (dy > 0.0f ? 1 : -1), 0, n - 1);
                    const std::size_t w0 = r.writes();
                    r.host.wheel(cx, ty, dy, false);
                    r.host.tick(1, kDt);
                    const std::span<const FakeWrite> ws = r.since(w0);
                    ok = ok && activeIndex(r.view(pid)) == want
                      && (want == from ? ws.empty()
                                       : ws.size() == 1 && ws[0].inGesture
                                             && funkgui::ease::sameBits(ws[0].value01, before.detents[want].host01));
                }
            r.host.tick(60, kDt);                                // the burst closes after 0.5 s idle
            const fcmp::probe::FakePort& port = r.facade.fakePort(pid);
            P.eq(name + ".wheel", ok && port.begins() == port.ends() && !port.inGesture() ? 1 : 0, 1);
            t.take(r);
        }
        {   // drag: canonical detents only, one gesture per drag
            Rig r(key);
            const funkgui::ValueView v = r.view(pid);
            r.host.drag(cx, ty, cx + 220.0f, ty, 22);
            r.host.tick(1, kDt);
            r.host.drag(cx, ty, cx - 440.0f, ty - 60.0f, 30);
            r.host.tick(1, kDt);
            bool ok = r.writes() > 0;
            for (const FakeWrite& w : r.facade.writes())
                ok = ok && w.pid == pid && w.inGesture && isDetent(v, w.value01);
            const fcmp::probe::FakePort& port = r.facade.fakePort(pid);
            P.eq(name + ".drag", ok && port.begins() == 2 && port.ends() == 2 ? 1 : 0, 1);
            t.take(r);
        }
        {   // detent-label clicks, where the labels are drawn
            Rig r(key);
            const funkgui::ValueView v = r.view(pid);
            if (labelsDrawn(v))
            {
                const int n = v.nDetents;
                const float cell = g.w / static_cast<float>(n);
                bool ok = true;
                for (int j = n - 1; j >= 0; --j)
                {
                    const funkgui::ValueView before = r.view(pid);
                    const int from = activeIndex(before);
                    const std::size_t w0 = r.writes();
                    r.host.click(g.x + cell * (static_cast<float>(j) + 0.5f), g.subTop() + 4.0f);
                    r.host.tick(1, kDt);
                    const std::span<const FakeWrite> ws = r.since(w0);
                    ok = ok && activeIndex(r.view(pid)) == j
                      && (j == from ? ws.empty()
                                    : ws.size() == 1 && ws[0].inGesture
                                          && funkgui::ease::sameBits(ws[0].value01, before.detents[j].host01));
                }
                P.eq(name + ".labels", ok ? 1 : 0, 1);
            }
            t.take(r);
        }
        {   // a11y: index space
            Rig r(key);
            const funkgui::ValueView v = r.view(pid);
            const int n = v.nDetents;
            bool ok = true;
            for (const int j : { n - 1, 0 })
            {
                const funkgui::ValueView before = r.view(pid);
                const std::size_t w0 = r.writes();
                r.panel.a11yAction(slotId(i), funkgui::A11yAction::setValue, static_cast<double>(j));
                r.host.tick(1, kDt);
                const std::span<const FakeWrite> ws = r.since(w0);
                ok = ok && activeIndex(r.view(pid)) == j
                  && (activeIndex(before) == j ? ws.empty()
                                               : ws.size() == 1 && ws[0].inGesture
                                                     && funkgui::ease::sameBits(ws[0].value01, before.detents[j].host01));
            }
            P.eq(name + ".a11y_set", ok ? 1 : 0, 1);
            t.take(r);
        }
    }

    // ---- continuous slots ---------------------------------------------------------------------------------------------

    // One key on a focused continuous slot: the write RuleSlider's rule gives for the view before it (02 §8.9).
    bool continuousKey(Rig& r, fcdsp::Pid pid, const char* key, float stepT, int to /* -1 home, 1 end, 2 default */)
    {
        const funkgui::ValueView before = r.view(pid);
        const ui::SlotModel& m = r.model(pid);
        float expected = 0.0f;
        if (to == 2)
            expected = m.defaultHost01();
        else
        {
            const float t = to < 0 ? 0.0f : to > 0 ? 1.0f : std::clamp(before.track, 0.0f, 1.0f) + stepT;
            expected = m.host01FromTrack(std::clamp(t, 0.0f, 1.0f));
        }
        const bool changes = !funkgui::ease::sameBits(r.facade.port(pid).value01(), expected);
        const std::size_t w0 = r.writes();
        r.press(key);
        const std::span<const FakeWrite> ws = r.since(w0);
        if (!changes)
            return ws.empty();
        return ws.size() == 1 && ws[0].pid == pid && ws[0].inGesture && funkgui::ease::sameBits(ws[0].value01, expected);
    }

    const funkgui::A11yItem* item(const Rig& r, uint32_t id, std::vector<funkgui::A11yItem>& store)
    {
        store = r.host.accessibility();
        for (const funkgui::A11yItem& it : store)
            if (it.id == id)
                return &it;
        return nullptr;
    }

    void continuousSlot(Probe& P, std::string_view key, std::size_t i, Tally& t)
    {
        const fcdsp::Pid pid = layout::kSlots[i].pid;
        const std::string name = "input." + pidName(pid);
        const funkgui::SlotGeom g = layout::slotGeom(layout::kSlots[i]);
        const float cx = g.x + 0.5f * g.w, ty = g.trackY();
        const bool hybrid = [&] {
            Rig r(key);
            const funkgui::ValueView v = r.view(pid);
            return v.nEndLo > 0 || v.nEndHi > 0;
        }();

        if (!hybrid)
        {   // keys (hybrid slots cross into end cells at the edges; none of the first eight Modes has one)
            Rig r(key);
            r.focus(slotId(i));
            bool ok = continuousKey(r, pid, "home", 0.0f, -1);
            ok = ok && continuousKey(r, pid, "up", 0.01f, 0);
            ok = ok && continuousKey(r, pid, "shift+up", 0.001f, 0);
            ok = ok && continuousKey(r, pid, "pageup", 0.1f, 0);
            ok = ok && continuousKey(r, pid, "down", -0.01f, 0);
            ok = ok && continuousKey(r, pid, "end", 0.0f, 1);
            ok = ok && continuousKey(r, pid, "delete", 0.0f, 2);
            P.eq(name + ".keys", ok ? 1 : 0, 1);
            t.take(r);
        }
        {   // double-click: the Mode default
            Rig r(key);
            r.focus(slotId(i));
            r.press("end");
            const float def = r.model(pid).defaultHost01();
            const bool changes = !funkgui::ease::sameBits(r.facade.port(pid).value01(), def);
            const std::size_t w0 = r.writes();
            r.host.doubleClick(cx, ty);
            r.host.tick(1, kDt);
            const std::span<const FakeWrite> ws = r.since(w0);
            const bool ok = changes ? !ws.empty() && ws.back().inGesture && funkgui::ease::sameBits(ws.back().value01, def)
                                    : ws.empty();
            const fcmp::probe::FakePort& port = r.facade.fakePort(pid);
            P.eq(name + ".dblclick", ok && port.begins() == port.ends() ? 1 : 0, 1);
            t.take(r);
        }
        {   // drag: one gesture, towards the far end of the track
            Rig r(key);
            const float dx = r.view(pid).track > 0.5f ? -60.0f : 60.0f;
            r.host.drag(cx, ty, cx + dx, ty, 6);
            r.host.tick(1, kDt);
            bool ok = r.writes() > 0;
            for (const FakeWrite& w : r.facade.writes())
                ok = ok && w.pid == pid && w.inGesture;
            const fcmp::probe::FakePort& port = r.facade.fakePort(pid);
            P.eq(name + ".drag", ok && port.begins() == 1 && port.ends() == 1 ? 1 : 0, 1);
            t.take(r);
        }
        {   // a11y setValue in display units: both ends of the Mode track
            Rig r(key);
            std::vector<funkgui::A11yItem> store;
            bool ok = true;
            for (const bool high : { true, false })
            {
                const funkgui::A11yItem* it = item(r, slotId(i), store);
                if (it == nullptr)
                {
                    ok = false;
                    break;
                }
                const double target = high ? it->hi : it->lo;
                r.panel.a11yAction(slotId(i), funkgui::A11yAction::setValue, target);
                r.host.tick(1, kDt);
                const funkgui::A11yItem* after = item(r, slotId(i), store);
                const double tol = 1.0e-3 * std::max(1.0, std::fabs(target));
                ok = ok && after != nullptr && std::fabs(after->v - target) <= tol;
            }
            for (const FakeWrite& w : r.facade.writes())
                ok = ok && w.pid == pid && w.inGesture;
            P.eq(name + ".a11y_set", ok ? 1 : 0, 1);
            t.take(r);
        }
    }

    // ---- refused slots: locked, derived, n/a ------------------------------------------------------------------------

    void refusedSlot(Probe& P, std::string_view key, std::size_t i, Tally& t)
    {
        const fcdsp::Pid pid = layout::kSlots[i].pid;
        const funkgui::SlotGeom g = layout::slotGeom(layout::kSlots[i]);
        const float cx = g.x + 0.5f * g.w, ty = g.trackY();
        Rig r(key);
        r.host.click(cx, ty);
        r.host.drag(cx, ty, cx + 200.0f, ty, 10);
        r.host.drag(cx, ty, cx - 200.0f, ty, 10);
        r.host.wheel(cx, ty, 1.0f, false);
        r.host.wheel(cx, ty, -3.0f, true);
        r.host.doubleClick(cx, ty);
        r.focus(slotId(i));
        r.press("up, down, home, end, delete, pageup, pagedown, shift+up");
        for (const funkgui::A11yAction a : { funkgui::A11yAction::setValue, funkgui::A11yAction::increment,
                                             funkgui::A11yAction::decrement })
            r.panel.a11yAction(slotId(i), a, 0.5);
        r.host.tick(60, kDt);
        P.eq("input." + pidName(pid) + ".refused", static_cast<int64_t>(r.writes()), 0);
        t.take(r);
    }

    // ---- the Tab order --------------------------------------------------------------------------------------------------

    void tabOrder(Probe& P, std::string_view key, Tally& t)
    {
        Rig r(key);
        const std::vector<funkgui::A11yItem> items = r.host.accessibility();
        std::vector<uint32_t> order;
        bool wraps = false;
        for (int k = 0; k < 1024; ++k)
        {
            r.host.keys("tab");
            const uint32_t id = r.panel.context().focus;
            if (!order.empty() && id == order.front())
            {
                wraps = true;
                break;
            }
            order.push_back(id);
        }
        std::vector<std::string> lines;
        for (std::size_t k = 0; k < order.size(); ++k)
        {
            const uint32_t id = order[k];
            const int v = ui::viewIndexOf(id);
            std::string line = std::to_string(k + 1) + " "
                             + (v >= 0 && v < ui::kSubViewCount ? kViewNames[static_cast<std::size_t>(v)] : "?") + ":"
                             + std::to_string(id & 0xFFFFu);
            for (const funkgui::A11yItem& it : items)
                if (it.id == id)
                    line += std::string(" ") + roleName(it.role) + " " + it.title;
            lines.push_back(line);
        }
        const std::set<uint32_t> unique(order.begin(), order.end());
        std::vector<uint32_t> slots, want;
        for (const uint32_t id : order)
            if (ui::viewIndexOf(id) == static_cast<int>(ui::ViewIndex::slotGrid))
                slots.push_back(id);
        for (std::size_t i = 0; i < layout::kSlots.size(); ++i)
        {
            want.push_back(slotId(i));
            const layout::SlotPlace& place = layout::kSlots[i];
            if (place.word == fcdsp::kNoPid)
                continue;
            const bool shown = fcdsp::idx(place.word) >= fcdsp::kNumModeParams
                            || r.panel.context().frame.res.view.p[fcdsp::idx(place.word)].state != fcdsp::SlotState::na;
            if (shown)
                want.push_back(wordId(i));
        }
        P.eq("input.taborder.wraps", wraps ? 1 : 0, 1);
        P.eq("input.taborder.unique", unique.size() == order.size() ? 1 : 0, 1);
        P.eq("input.taborder.slots", slots == want ? 1 : 0, 1);
        P.lines("taborder", lines);
        t.take(r);
    }

    // ---- words ------------------------------------------------------------------------------------------------------------

    void word(Probe& P, std::string_view key, fcdsp::Pid slotPid, Tally& t)
    {
        const std::size_t i = slotIndex(slotPid);
        const layout::SlotPlace& place = layout::kSlots[i];
        const fcdsp::Pid w = place.word;
        const funkgui::Rect hit = funkgui::AttachedWord::hitFor(layout::slotGeom(place));
        Rig r(key);
        const bool modeFiltered = fcdsp::idx(w) < fcdsp::kNumModeParams;
        const fcdsp::SlotState st = modeFiltered ? r.panel.context().frame.res.view.p[fcdsp::idx(w)].state
                                                 : fcdsp::SlotState::live;
        const auto on = [&] {
            return modeFiltered ? r.panel.context().frame.res.view.p[fcdsp::idx(w)].plain >= 0.5f
                                : r.facade.port(w).value01() >= 0.5f;
        };
        bool ok = true;
        if (st == fcdsp::SlotState::live || st == fcdsp::SlotState::stepped)
        {
            for (int k = 0; k < 2; ++k)
            {
                const bool before = on();
                const std::size_t w0 = r.writes();
                if (k == 0)
                    r.host.click(hit.centreX(), hit.centreY());
                else
                {
                    r.focus(wordId(i));
                    r.host.keys("return");
                }
                r.host.tick(1, kDt);
                const std::span<const FakeWrite> ws = r.since(w0);
                ok = ok && on() != before && ws.size() == 1 && ws[0].pid == w && ws[0].inGesture
                  && funkgui::ease::sameBits(ws[0].value01, fcdsp::toNorm(w, before ? 0.0f : 1.0f));
            }
        }
        else
        {
            // Hidden (n/a) or refused (locked): the switch is never written.
            r.host.click(hit.centreX(), hit.centreY());
            r.host.tick(1, kDt);
            for (const FakeWrite& fw : r.facade.writes())
                ok = ok && fw.pid != w;
        }
        P.eq("input.word." + lowerAscii(place.wordLabel), ok ? 1 : 0, 1);
        t.take(r);
    }

    // ---- Mode switches and the defaults batch --------------------------------------------------------------------------

    const fcdsp::ModeSlot& otherMode(const fcdsp::ModeEntry& entry)
    {
        const std::span<const fcdsp::ModeSlot> all = fcdsp::modeSlots();
        std::size_t k = 0;
        for (; k < all.size(); ++k)
            if (all[k].entry == &entry)
                break;
        return all[(k + 1) % all.size()];
    }

    void modeSwitch(Probe& P, const fcdsp::ModeEntry& entry, Tally& t)
    {
        const fcdsp::ModeSlot& other = otherMode(entry);
        const int here = fcdsp::slotOf(entry);
        Rig r(entry.desc->key);
        funkgui::ParamPort& modePort = r.facade.port(fcdsp::Pid::mode);
        bool only = true;
        int frames = 0;
        for (const int slot : { static_cast<int>(other.slot), here })
        {
            const std::size_t w0 = r.writes();
            r.gestures().tap(modePort, fcdsp::toNorm(fcdsp::Pid::mode, static_cast<float>(slot)));   // the latch's tap
            r.host.tick(1, kDt);                                 // the host's next frame (EditorHost idles at 12 Hz)
            frames = std::max(frames, r.host.settle(kMaxSettle, kDt));
            const std::span<const FakeWrite> ws = r.since(w0);
            const bool ok = ws.size() == 1 && ws[0].pid == fcdsp::Pid::mode && ws[0].inGesture
                         && r.panel.context().frame.res.view.slot == static_cast<uint8_t>(slot);
            if (!ok)
            {
                std::printf("NOTE     mode switch to slot %d: now slot %d, %zu writes:", slot,
                            static_cast<int>(r.panel.context().frame.res.view.slot), ws.size());
                for (const FakeWrite& w : ws)
                    std::printf(" %s=%g%s", pidName(w.pid).c_str(), static_cast<double>(w.value01),
                                w.inGesture ? "" : "(outside a gesture)");
                std::printf("\n");
            }
            only = only && ok;
        }
        P.eq("input.mode_switch.only_mode", only ? 1 : 0, 1);
        P.le("input.mode_switch.settle_frames", frames, kMaxSettle);
        t.take(r);

        // A host (automation, a preset) moves the Mode: the landing writes nothing.
        Rig h(entry.desc->key);
        h.facade.setMode(other.key);
        h.host.tick(1, kDt);
        const int f1 = h.host.settle(kMaxSettle, kDt);
        h.facade.setMode(entry.desc->key);
        h.host.tick(1, kDt);
        const int f2 = h.host.settle(kMaxSettle, kDt);
        P.eq("input.mode_switch.landing", h.writes() == 0 && f1 <= kMaxSettle && f2 <= kMaxSettle ? 1 : 0, 1);
        t.take(h);
    }

    void defaultsBatch(Probe& P, const fcdsp::ModeEntry& entry, Tally& t)
    {
        const fcdsp::ModeSlot& other = otherMode(entry);
        Rig r(entry.desc->key);
        fcdsp::RawParams raw = r.facade.currentRaw();
        raw.modeSlot = other.slot;
        fcdsp::modeDefaults(*other.entry->desc, raw);
        std::vector<std::pair<funkgui::ParamPort*, float>> writes;
        writes.emplace_back(&r.facade.port(fcdsp::Pid::mode), fcdsp::toNorm(fcdsp::Pid::mode, static_cast<float>(other.slot)));
        for (std::size_t k = 0; k < fcdsp::kNumModeParams; ++k)
        {
            const auto pid = static_cast<fcdsp::Pid>(k);
            writes.emplace_back(&r.facade.port(pid), fcdsp::toNorm(pid, raw.v[k]));
        }
        std::size_t changing = 0;
        for (const auto& [port, v] : writes)
            changing += funkgui::ease::sameBits(port->value01(), std::clamp(v, 0.0f, 1.0f)) ? 0 : 1;
        r.gestures().tapMany(writes);
        bool ok = r.writes() == changing && changing >= 1;
        for (const FakeWrite& w : r.facade.writes())
            ok = ok && w.inGesture && w.batchDepth == 1;
        ok = ok && r.facade.batches() == 1 && r.facade.batchDepth() == 0 && r.host.log.batches == 1
          && r.host.log.batchDepth == 0;
        r.host.tick(1, kDt);
        r.host.settle(kMaxSettle, kDt);
        ok = ok && r.panel.context().frame.res.view.slot == other.slot;
        if (!ok)
            std::printf("NOTE     defaults batch: %zu writes for %zu changes, facade batches %d depth %d, host batches %d "
                        "depth %d, slot %d (want %d)\n", r.writes(), changing, r.facade.batches(), r.facade.batchDepth(),
                        r.host.log.batches, r.host.log.batchDepth,
                        static_cast<int>(r.panel.context().frame.res.view.slot), static_cast<int>(other.slot));
        P.eq("input.batch.defaults", ok ? 1 : 0, 1);
        t.take(r);
    }

    // ---- handled keys return true (02 §8.9: "Handled keys return true, so Logic and Live do not swallow them"; S13 H1a) --

    funkgui::KeyEvent keyOf(funkgui::Key k, bool shift = false)
    {
        funkgui::KeyEvent e;
        e.key = k;
        e.mods.shift = shift;
        return e;
    }

    const char* keyName(const funkgui::KeyEvent& e)
    {
        switch (e.key)
        {
            case funkgui::Key::character: return "character";
            case funkgui::Key::tab:       return e.mods.shift ? "shift+tab" : "tab";
            case funkgui::Key::up:        return "up";
            case funkgui::Key::down:      return "down";
            case funkgui::Key::left:      return "left";
            case funkgui::Key::right:     return "right";
            case funkgui::Key::pageUp:    return "pageup";
            case funkgui::Key::pageDown:  return "pagedown";
            case funkgui::Key::home:      return "home";
            case funkgui::Key::end:       return "end";
            case funkgui::Key::escape:    return "escape";
            case funkgui::Key::enter:     return "return";
            case funkgui::Key::backspace: return "backspace";
            case funkgui::Key::del:       return "delete";
            case funkgui::Key::space:     return "space";
        }
        return "?";
    }

    // The keys a Tab stop takes (02 §8.9's key table), the ones that change what is shown last: a slot or a handle (a
    // slider, or the static text of an n/a slot), whatever its state, every value key; a radio group the arrows and
    // Home / End, then Return / Space; a latch, a word or a button Return / Space; the Mode latch the arrows, Home / End,
    // Return / Space; the preset strip's name the arrows and Return / Space; a browser row the arrows, Home / End, then
    // Return. Every stop also takes Tab and Shift-Tab (the Panel moves the focus) and, the ring shown, Esc.
    std::vector<funkgui::KeyEvent> keysFor(int view, funkgui::A11yRole role)
    {
        using K = funkgui::Key;
        std::vector<funkgui::KeyEvent> ks { keyOf(K::tab), keyOf(K::tab, true) };
        const auto add = [&](std::initializer_list<K> list) {
            for (const K k : list)
                ks.push_back(keyOf(k));
        };
        switch (role)
        {
            case funkgui::A11yRole::slider:
            case funkgui::A11yRole::staticText:
                add({ K::up, K::down, K::left, K::right, K::pageUp, K::pageDown, K::home, K::end, K::del });
                break;
            case funkgui::A11yRole::radioGroup:
            case funkgui::A11yRole::radioButton:
                add({ K::up, K::down, K::left, K::right, K::home, K::end, K::enter, K::space });
                break;
            case funkgui::A11yRole::comboBox:
                if (view == static_cast<int>(ui::ViewIndex::header))
                    add({ K::up, K::down, K::left, K::right, K::home, K::end, K::enter, K::space });
                else
                    add({ K::up, K::down, K::left, K::right, K::enter, K::space });
                break;
            case funkgui::A11yRole::listItem:
                add({ K::up, K::down, K::left, K::right, K::home, K::end, K::enter });
                break;
            case funkgui::A11yRole::toggleButton:
            case funkgui::A11yRole::button:
                add({ K::enter, K::space });
                break;
            case funkgui::A11yRole::image:
            case funkgui::A11yRole::progressBar:
                break;
        }
        ks.push_back(keyOf(K::escape));
        return ks;
    }

    // The machine-wide preferences a stop's keys change (the HISTORY span and GR view, the meter scale, the theme; 02
    // §5.9): taken once, and put back before every stop, so each stop starts from the panel its Tab order was read on.
    struct Prefs
    {
        static constexpr int kLo = std::numeric_limits<int>::min(), kHi = std::numeric_limits<int>::max();
        int theme = 0, span = 0, view = 0, scale = 0;

        Prefs()
        {
            const funkgui::UiPreferences& p = funkgui::UiPreferences::get();
            theme = p.theme();
            span = p.getInt("historySpanTenths", layout::kDefaultSpanTenths, kLo, kHi);
            view = p.getInt("grView", layout::vu::kDefaultView, kLo, kHi);
            scale = p.getInt("meterScaleDb", layout::kDefaultScaleDb, kLo, kHi);
        }

        void restore() const
        {
            funkgui::UiPreferences& p = funkgui::UiPreferences::get();
            p.setTheme(theme);
            p.setInt("historySpanTenths", span);
            p.setInt("grView", view);
            p.setInt("meterScaleDb", scale);
        }
    };

    void handledKeys(Probe& P, std::string_view key, Tally& t)
    {
        const Prefs prefs;
        int stops = 0, keys = 0, misses = 0;
        for (const ui::ViewSpec& v : ui::views())
        {
            // The view's Tab order and each stop's role.
            std::vector<std::pair<uint32_t, funkgui::A11yRole>> order;
            {
                prefs.restore();
                Rig r(key);
                r.panel.setView(v, true);
                r.host.tick(1, kDt);                               // the new screen's plots tick at least once
                r.host.settle(kMaxSettle, kDt);
                const std::vector<funkgui::A11yItem> items = r.host.accessibility();
                std::vector<std::string> lines;
                for (int k = 0; k < 1024; ++k)
                {
                    r.host.keys("tab");
                    const uint32_t id = r.panel.context().focus;
                    if (id == 0 || (!order.empty() && id == order.front().first))
                        break;
                    funkgui::A11yRole role = funkgui::A11yRole::image;
                    std::string title;
                    for (const funkgui::A11yItem& it : items)
                        if (it.id == id)
                        {
                            role = it.role;
                            title = it.title;
                        }
                    order.emplace_back(id, role);
                    const int vi = ui::viewIndexOf(id);
                    const char* viewName = vi >= 0 && vi < ui::kSubViewCount
                                               ? kViewNames[static_cast<std::size_t>(vi)] : "?";
                    lines.push_back(std::to_string(order.size()) + " " + viewName + ":" + std::to_string(id & 0xFFFFu)
                                    + " " + roleName(role) + " " + title);
                }
                // The other views' Tab orders as golden lines too (the panel's is `taborder`, above): the
                // Characteristics screen's (02 §7.5) and each open browser's (S13 H1a), frozen at FZ5 with it.
                if (std::string_view(v.id) != "panel")
                    P.lines(std::string("taborder.") + v.id, lines);
            }
            for (const auto& [id, role] : order)
            {
                ++stops;
                prefs.restore();
                Rig r(key);
                r.panel.setView(v, true);
                r.host.tick(1, kDt);                               // the new screen's plots tick at least once
                r.host.settle(kMaxSettle, kDt);
                for (const funkgui::KeyEvent& e : keysFor(ui::viewIndexOf(id), role))
                {
                    r.focus(id);                                   // the stop, its ring shown, before every key
                    const bool used = r.panel.key(e);
                    r.host.tick(1, kDt);
                    ++keys;
                    if (!used)
                    {
                        ++misses;
                        std::printf("NOTE     %s: %s:%u %s returned false for %s\n", v.id,
                                    kViewNames[static_cast<std::size_t>(ui::viewIndexOf(id))], id & 0xFFFFu,
                                    roleName(role), keyName(e));
                    }
                }
                r.host.tick(60, kDt);                                // a wheel-style burst closes
                t.take(r);
            }
        }
        prefs.restore();
        P.ge("input.keys.stops", stops, 1);
        P.ge("input.keys.pressed", keys, 1);
        P.eq("input.keys.handled", misses, 0);
    }

    void popup(Probe& P, std::string_view key, Tally& t)
    {
        Rig r(key);
        const funkgui::SlotGeom g = layout::slotGeom(layout::kSlots[0]);
        funkgui::Mods ctrl;
        ctrl.ctrl = true;
        r.host.click(g.x + 0.5f * g.w, g.trackY(), ctrl);
        r.host.tick(1, kDt);
        P.eq("input.popup", r.host.log.menus == 1 && r.writes() == 0 ? 1 : 0, 1);
        t.take(r);
    }
}

FCMP_PROBE(ui, input)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.input: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    {
        Rig r(C.key);
        if (!r.settled)
        {
            P.harnessError("ui.input: the panel did not settle within 600 frames");
            return P.finish();
        }
    }
    Tally tally;
    tabOrder(P, C.key, tally);
    for (std::size_t i = 0; i < layout::kSlots.size(); ++i)
    {
        funkgui::ValueView v;
        {
            Rig r(C.key);
            v = r.view(layout::kSlots[i].pid);
        }
        switch (v.state)
        {
            case funkgui::ValueState::stepped:    steppedSlot(P, C.key, i, tally); break;
            case funkgui::ValueState::continuous: continuousSlot(P, C.key, i, tally); break;
            case funkgui::ValueState::locked:
            case funkgui::ValueState::derived:
            case funkgui::ValueState::na:         refusedSlot(P, C.key, i, tally); break;
        }
    }
    popup(P, C.key, tally);
    word(P, C.key, fcdsp::Pid::makeup, tally);
    word(P, C.key, fcdsp::Pid::det, tally);
    word(P, C.key, fcdsp::Pid::schpf, tally);
    modeSwitch(P, *entry, tally);
    defaultsBatch(P, *entry, tally);
    handledKeys(P, C.key, tally);
    P.ge("input.writes.checked", tally.writes, 1);
    P.eq("input.writes.outside_gesture", tally.outside, 0);
    return P.finish();
}
