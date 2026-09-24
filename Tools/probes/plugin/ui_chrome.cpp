// FCMP_PROBE layer=ui name=chrome scope=mode timeout=300
//
// ui.chrome.<key> (S6.3, U1b): the header, the display row and the footer, driven through the Panel API and
// HeadlessHost input over a FakeFacade (Panel{skipHint, syncPreview}; settle at 1/60 s). Spec rows only.
//
//   mode.*       the Mode latch writes only `mode`, one gesture per click, key or wheel burst, never inside a batch
//                (02 §8.4.3, K2 #4): › and ‹ step the global order (Group, then slot) and wrap; the wheel over the
//                name is one burst and stops at the ends; ←/→ Home/End on the focused latch; a click on the name
//                opens the Mode browser (and nothing is written), a second click closes it; the a11y comboBox
//                reads the Mode name and the group line "VCA · 1 OF 8"; the name crossfade settles; the
//                Mode-switch summary shows for 3 s.
//   cells.*      each QUALITY / LOOKAHEAD cell writes its own port once (host01 = i / 2) and nothing else; the
//                active cell writes nothing (K2 #6: the UI never changes latency itself); a focused group steps
//                with the arrows.
//   latch.*      DELTA and BYPASS toggle their ports with one gesture each; dragging off cancels; CHARACTERISTICS
//                switches the screen (UiState::charExpanded follows) and writes no parameter.
//   footer.*     the footer shows the hovered item's spec line (chrome items); the hovered slot's reason — once the
//                slot grid offers the hand (U1s): with the U1a stub grid that row is a NOTE; the display row
//                shows the hovered slot.
//   theme.*      a THEME cell writes the preference (never a parameter), and the panel's geometry is the same in
//                the other theme (the swap is geometry-invariant).
//   notice.*     StateNotice texts (newer session, unknown/retired Mode, revision K2 #10), a load while the editor
//                is open, kUiPoisonReset (3 s), and their expiry.
//   lookahead.*  wantsLookahead && budget OFF: the hint over the band and on LOOKAHEAD; gone with a budget; never
//                for a Mode that does not want lookahead.
//   display.*    GAIN REDUCTION: "–" when not live, the live GR (live-tagged) and 0.0, stale after 0.5 s.
//   tab.*        the chrome's Tab stops: the Mode latch first, then QUALITY, LOOKAHEAD, DELTA, BYPASS,
//                CHARACTERISTICS in that order, THEME last.
//
// THEME writes UiPreferences: the probe refuses to run without FCMP_PREFS_DIR (CTest sets a sandbox), so it never
// touches the user's real preferences file.
//
// Review pictures (not a test): `fcmp_probe_plugin ui.chrome --mode <key> … -- --png-dir <dir>` also writes PNGs of the
// states above that a settled ui.dump cannot show: chrome-<key>-{hover-delta,live,focus,summary,fade,notice,bypass-ramp,
// lookahead,chars}.png (dpi 2, theme 0).
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "editor/Tags.h"

#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Fingerprint.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/prefs/UiPreferences.h>
#include <funkgui/text/FontService.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <crt_externs.h>                                         // _NSGetArgc / _NSGetArgv (macOS)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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

    constexpr std::array<const char*, 8> kGroupNames { "VCA", "FET", "OPTO", "VARI-MU", "DIODE", "MODERN", "LIMIT",
                                                       "OTHER" };
    constexpr const char* kNewer = "SESSION FROM A NEWER FCOMPRESSOR \xE2\x80\x94 LOADED BEST EFFORT";
    constexpr const char* kPoison = "AUDIO RESET AFTER A NON-FINITE SAMPLE";

    int b(bool v) { return v ? 1 : 0; }

    // A FakeFacade, a Panel over it and a HeadlessHost, settled; `prep` runs on the facade before the Panel exists.
    struct Rig
    {
        template <class Prep>
        Rig(std::string_view key, Prep&& prep, int theme = 0)
            : facade(key), prepared((prep(facade), true)), panel(facade, kProbeOptions), host(panel, theme, 2.0f)
        {
            settled = host.settle(kMaxSettle, kDt);
            facade.resetCounts();
        }
        explicit Rig(std::string_view key, int theme = 0) : Rig(key, [](FakeFacade&) {}, theme) {}

        Rig(const Rig&) = delete;
        Rig& operator=(const Rig&) = delete;

        const ui::PanelContext& ctx() const { return panel.context(); }
        uint8_t slot() const { return facade.currentRaw().modeSlot; }
        int settle() { return host.settle(kMaxSettle, kDt); }

        FakeFacade            facade;
        bool                  prepared;
        ui::Panel             panel;
        funkgui::HeadlessHost host;
        int                   settled = 0;
    };

    funkgui::Point centre(const funkgui::Rect& r) { return { r.centreX(), r.centreY() }; }

    const funkgui::A11yItem* findItem(const std::vector<funkgui::A11yItem>& items, ui::ViewIndex v,
                                      funkgui::A11yRole role, std::string_view title = {})
    {
        for (const funkgui::A11yItem& it : items)
            if (ui::viewIndexOf(it.id) == static_cast<int>(v) && it.role == role && (title.empty() || it.title == title))
                return &it;
        return nullptr;
    }

    std::string itemValue(const Rig& r, ui::ViewIndex v, std::string_view title)
    {
        const std::vector<funkgui::A11yItem> items = r.host.accessibility();
        const funkgui::A11yItem* it = findItem(items, v, funkgui::A11yRole::staticText, title);
        return it != nullptr ? it->value : std::string("<no item>");
    }

    std::string footerLine(const Rig& r) { return itemValue(r, ui::ViewIndex::footer, "Footer"); }
    std::string displayValue(const Rig& r) { return itemValue(r, ui::ViewIndex::displayRow, "Display"); }

    bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
    bool contains(std::string_view s, std::string_view p) { return s.find(p) != std::string_view::npos; }

    // Gesture begins over every port (a Mode change writes nothing else, K2 #4; the cells only their own port, K2 #6).
    int beginsExcept(FakeFacade& f, Pid except)
    {
        int n = 0;
        for (std::size_t i = 0; i < fcdsp::kNumParams; ++i)
            if (static_cast<Pid>(i) != except)
                n += f.fakePort(static_cast<Pid>(i)).begins();
        return n;
    }

    // Exactly one write, to `pid`, inside its gesture and outside any batch; one begin and one end on that port and
    // no gesture on any other.
    bool oneTap(FakeFacade& f, Pid pid)
    {
        const auto w = f.writes();
        const fcmp::probe::FakePort& p = f.fakePort(pid);
        return w.size() == 1 && w[0].pid == pid && w[0].inGesture && w[0].batchDepth == 0 && p.begins() == 1
            && p.ends() == 1 && !p.inGesture() && beginsExcept(f, pid) == 0 && f.batches() == 0;
    }

    bool nothingWritten(FakeFacade& f) { return f.writes().empty() && beginsExcept(f, fcdsp::kNoPid) == 0; }

    // The registered Modes in the header's global order (02 §8.5): Group, then slot.
    std::vector<const fcdsp::ModeSlot*> globalOrder()
    {
        std::vector<const fcdsp::ModeSlot*> v;
        for (const fcdsp::ModeSlot& s : fcdsp::modeSlots())
            if (s.entry != nullptr && s.entry->desc != nullptr)
                v.push_back(&s);
        std::stable_sort(v.begin(), v.end(), [](const fcdsp::ModeSlot* a, const fcdsp::ModeSlot* c) {
            const auto ga = static_cast<int>(a->entry->desc->group), gc = static_cast<int>(c->entry->desc->group);
            return ga != gc ? ga < gc : a->slot < c->slot;
        });
        return v;
    }

    // ---- the Mode latch -------------------------------------------------------------------------------------------------

    void modeLatch(Probe& P, std::string_view key, const fcdsp::ModeDescriptor& desc)
    {
        const std::vector<const fcdsp::ModeSlot*> order = globalOrder();
        const int n = static_cast<int>(order.size());
        int pos = -1;
        for (int i = 0; i < n; ++i)
            if (order[static_cast<std::size_t>(i)]->key == key)
                pos = i;
        if (pos < 0 || n < 2)
        {
            P.harnessError("ui.chrome: '" + std::string(key) + "' is not in the global Mode order");
            return;
        }
        const auto slotAt = [&](int i) { return order[static_cast<std::size_t>(i)]->slot; };
        const funkgui::Point next = centre(L::header::kModeNext);
        const funkgui::Point prev = centre(L::header::kModePrev);
        const funkgui::Point name = centre(L::header::kModeName);

        {   // › then ‹: one tap each, wrapping; the summary; the crossfade settles
            Rig r(key);
            P.le("mode.settle_initial", r.settled, kMaxSettle);
            r.host.click(next.x, next.y);
            P.eq("mode.next.one_gesture", b(oneTap(r.facade, Pid::mode)), 1);
            P.eq("mode.next.slot", r.slot(), slotAt((pos + 1) % n));
            r.host.tick(1, kDt);
            const fcdsp::ModeEntry* to = fcdsp::bySlot(slotAt((pos + 1) % n));
            const std::string toName(to != nullptr ? to->desc->name : std::string_view("?"));
            const std::string summary = footerLine(r);
            P.eq("mode.summary.shown", b(startsWith(summary, toName + " \xC2\xB7 ")), 1);
            const int frames = r.settle();
            P.in("mode.crossfade.settles", frames, 1, kMaxSettle);
            r.host.tick(static_cast<int>(3.1f / kDt), kDt);
            P.eq("mode.summary.expires", b(footerLine(r) != summary), 1);

            r.facade.resetCounts();
            r.host.click(prev.x, prev.y);
            P.eq("mode.prev.one_gesture", b(oneTap(r.facade, Pid::mode)), 1);
            P.eq("mode.prev.slot", r.slot(), slotAt(pos));
        }
        {   // ‹ from the first Mode wraps to the last
            Rig r(order.front()->key);
            r.host.click(prev.x, prev.y);
            P.eq("mode.prev.wraps", b(oneTap(r.facade, Pid::mode) && r.slot() == slotAt(n - 1)), 1);
        }
        {   // the wheel over the name: one gesture per burst, stops at the ends
            Rig r(key);
            const int dir = pos < n - 1 ? 1 : -1;                // towards the side with room
            const int room = dir > 0 ? n - 1 - pos : pos;
            for (int i = 0; i < 4; ++i)                          // 4 notches: near an end the extra ones write nothing
                r.host.wheel(name.x, name.y, static_cast<float>(dir));
            r.host.tick(static_cast<int>(0.6f / kDt), kDt);      // the burst closes after 0.5 s idle
            const fcmp::probe::FakePort& m = r.facade.fakePort(Pid::mode);
            const int expectedSets = std::min(4, room);
            P.eq("mode.wheel.one_burst", b(m.begins() == 1 && m.ends() == 1 && !m.inGesture()), 1);
            P.eq("mode.wheel.sets", m.sets(), expectedSets);
            P.eq("mode.wheel.only_mode", beginsExcept(r.facade, Pid::mode), 0);
            P.eq("mode.wheel.slot", r.slot(), slotAt(std::clamp(pos + dir * expectedSets, 0, n - 1)));
            P.eq("mode.wheel.no_batch", r.facade.batches(), 0);
        }
        {   // keys on the focused latch
            Rig r(key);
            const std::vector<funkgui::A11yItem> items = r.host.accessibility();
            const funkgui::A11yItem* latch = findItem(items, ui::ViewIndex::header, funkgui::A11yRole::comboBox);
            if (latch == nullptr)
            {
                P.harnessError("ui.chrome: no Mode latch comboBox");
                return;
            }
            r.panel.a11yAction(latch->id, funkgui::A11yAction::focus);
            int changes = 0;
            int at = pos;
            const auto press = [&](const char* k, int want) {
                r.host.keys(k);
                if (want != at)
                    ++changes;
                at = want;
                return r.slot() == slotAt(want);
            };
            const bool home = press("home", 0);
            const bool end = press("end", n - 1);
            const bool left = press("left", n - 2);
            const bool right = press("right", n - 1);
            const bool stop = press("right", n - 1);             // at the end: no wrap, no write
            const fcmp::probe::FakePort& m = r.facade.fakePort(Pid::mode);
            P.eq("mode.keys.slots", b(home && end && left && right && stop), 1);
            P.eq("mode.keys.gestures", b(m.begins() == changes && m.ends() == changes && m.sets() == changes), 1);
            P.eq("mode.keys.only_mode", beginsExcept(r.facade, Pid::mode), 0);
            r.host.keys("return");
            r.settle();
            P.eq("mode.keys.return_opens_browser", b(r.panel.overlay() == ui::Overlay::modeBrowser), 1);
        }
        {   // the name opens the browser (no write); a second click closes it; a11y
            Rig r(key);
            r.host.click(name.x, name.y);
            r.settle();
            P.eq("mode.browser.opens", b(r.panel.overlay() == ui::Overlay::modeBrowser && nothingWritten(r.facade)), 1);
            r.host.click(name.x, name.y);
            r.settle();
            P.eq("mode.browser.closes", b(r.panel.overlay() == ui::Overlay::none && nothingWritten(r.facade)), 1);
            r.host.drag(name.x, name.y, name.x, name.y + 200.0f);   // pressed on the name, released off it: cancelled
            r.settle();
            P.eq("mode.browser.drag_off", b(r.panel.overlay() == ui::Overlay::none && nothingWritten(r.facade)), 1);

            const std::vector<funkgui::A11yItem> items = r.host.accessibility();
            const funkgui::A11yItem* latch = findItem(items, ui::ViewIndex::header, funkgui::A11yRole::comboBox);
            const auto g = static_cast<std::size_t>(desc.group);
            const std::string group = std::string(g < kGroupNames.size() ? kGroupNames[g] : "OTHER") + " \xC2\xB7 "
                                    + std::to_string(pos + 1) + " OF " + std::to_string(n);
            P.eq("mode.a11y.value", b(latch != nullptr && latch->value == std::string(desc.name)), 1);
            P.eq("mode.a11y.group_line", b(latch != nullptr && latch->description == group), 1);
            P.eq("mode.a11y.tab_stop", b(latch != nullptr && latch->title == "Mode"), 1);
        }
    }

    // ---- QUALITY / LOOKAHEAD cells ------------------------------------------------------------------------------------

    void cells(Probe& P, std::string_view key)
    {
        struct Group { const char* row; const char* title; Pid pid; const std::array<funkgui::Rect, 3>* rects; };
        const std::array<Group, 2> groups { { { "quality", "Quality", Pid::quality, &L::display::kQualityCells },
                                              { "lookahead", "Lookahead budget", Pid::labudget,
                                                &L::display::kLookaheadCells } } };
        for (const Group& grp : groups)
            for (int i = 0; i < 3; ++i)
            {
                Rig r(key);
                const int before = static_cast<int>(std::lround(r.facade.fakePort(grp.pid).plain()));
                const funkgui::Point p = centre((*grp.rects)[static_cast<std::size_t>(i)]);
                r.host.click(p.x, p.y);
                const std::string row = std::string("cells.") + grp.row + "." + std::to_string(i);
                if (i == before)
                    P.eq(row + ".active_writes_nothing", b(nothingWritten(r.facade)), 1);
                else
                {
                    const auto w = r.facade.writes();
                    P.eq(row + ".one_gesture", b(oneTap(r.facade, grp.pid)), 1);
                    P.near(row + ".host01", w.empty() ? -1.0 : static_cast<double>(w[0].value01),
                           static_cast<double>(i) / 2.0, 1.0e-6);
                    P.eq(row + ".plain", static_cast<int>(std::lround(r.facade.fakePort(grp.pid).plain())), i);
                }
                const std::vector<funkgui::A11yItem> items = r.host.accessibility();
                const funkgui::A11yItem* g = findItem(items, ui::ViewIndex::displayRow, funkgui::A11yRole::radioGroup,
                                                      grp.title);
                int checked = -1;
                for (const funkgui::A11yItem& it : items)
                    if (g != nullptr && it.parent == g->id && it.role == funkgui::A11yRole::radioButton && it.checked)
                        checked = static_cast<int>(it.id - g->id - 1);
                P.eq(row + ".a11y_checked", checked, i);
            }

        {   // the focused QUALITY group steps with the arrows (one tap)
            Rig r(key);
            const std::vector<funkgui::A11yItem> items = r.host.accessibility();
            const funkgui::A11yItem* g = findItem(items, ui::ViewIndex::displayRow, funkgui::A11yRole::radioGroup,
                                                  "Quality");
            if (g != nullptr)
                r.panel.a11yAction(g->id, funkgui::A11yAction::focus);
            const int before = static_cast<int>(std::lround(r.facade.fakePort(Pid::quality).plain()));
            r.host.keys(before < 2 ? "right" : "left");
            P.eq("cells.keys.one_gesture", b(g != nullptr && oneTap(r.facade, Pid::quality)), 1);
            P.eq("cells.keys.step", static_cast<int>(std::lround(r.facade.fakePort(Pid::quality).plain())),
                 before < 2 ? before + 1 : before - 1);
        }
        {   // QUALITY and LOOKAHEAD help is formatted from kOs / budgetMs (K1 #29)
            Rig r(key);
            const std::vector<funkgui::A11yItem> items = r.host.accessibility();
            const funkgui::A11yItem* g = findItem(items, ui::ViewIndex::displayRow, funkgui::A11yRole::radioGroup,
                                                  "Quality");
            std::string stdHelp;
            for (const funkgui::A11yItem& it : items)
                if (g != nullptr && it.id == g->id + 2)
                    stdHelp = it.help;
            const std::string want = "Std: " + std::to_string(fcdsp::kOs[1].factor) + " times IIR oversampling, "
                                   + std::to_string(fcdsp::kOs[1].latency) + " samples latency";
            P.eq("cells.quality.help_from_kos", b(stdHelp == want), 1);
        }
    }

    // ---- latches --------------------------------------------------------------------------------------------------------

    void latches(Probe& P, std::string_view key)
    {
        struct Latch { const char* row; Pid pid; funkgui::Rect rect; };
        const std::array<Latch, 2> params { { { "delta", Pid::delta, L::display::kDelta },
                                              { "bypass", Pid::bypass, L::display::kBypass } } };
        for (const Latch& l : params)
        {
            Rig r(key);
            const funkgui::Point p = centre(l.rect);
            r.host.click(p.x, p.y);
            const bool on = oneTap(r.facade, l.pid) && r.facade.fakePort(l.pid).value01() >= 0.5f;
            P.eq(std::string("latch.") + l.row + ".on", b(on), 1);
            r.facade.resetCounts();
            r.host.click(p.x, p.y);
            const bool off = oneTap(r.facade, l.pid) && r.facade.fakePort(l.pid).value01() < 0.5f;
            P.eq(std::string("latch.") + l.row + ".off", b(off), 1);
            r.facade.resetCounts();
            r.host.drag(p.x, p.y, 480.0f, 300.0f);                // pressed, dragged off, released outside
            P.eq(std::string("latch.") + l.row + ".drag_off_cancels", b(nothingWritten(r.facade)), 1);
        }
        {
            Rig r(key);
            const funkgui::Point p = centre(L::display::kCharacteristics);
            r.host.click(p.x, p.y);
            const int frames = r.settle();
            P.eq("latch.chars.on", b(r.panel.screen() == ui::Screen::characteristics && r.facade.uiState().charExpanded
                                     && nothingWritten(r.facade)), 1);
            P.in("latch.chars.crossfade_settles", frames, 1, kMaxSettle);
            const std::vector<funkgui::A11yItem> items = r.host.accessibility();
            const funkgui::A11yItem* t = findItem(items, ui::ViewIndex::displayRow, funkgui::A11yRole::toggleButton,
                                                  "CHARACTERISTICS");
            P.eq("latch.chars.a11y_checked", b(t != nullptr && t->checked), 1);
            r.host.click(p.x, p.y);
            r.settle();
            P.eq("latch.chars.off", b(r.panel.screen() == ui::Screen::panel && !r.facade.uiState().charExpanded
                                      && nothingWritten(r.facade)), 1);
        }
    }

    // ---- footer spec lines, the hovered slot's reason, the display's hovered slot -------------------------------------

    void footer(Probe& P, std::string_view key)
    {
        struct Hover { const char* row; funkgui::Rect rect; const char* prefix; };
        const std::array<Hover, 5> hovers { {
            { "delta", L::display::kDelta, "DELTA" },
            { "characteristics", L::display::kCharacteristics, "CHARACTERISTICS" },
            { "quality", L::display::kQualityCells[1], "QUALITY" },
            { "mode", L::header::kModeName, "MODE" },
            { "theme", L::footer::kThemeCells[1], "THEME" },
        } };
        for (const Hover& h : hovers)
        {
            Rig r(key);
            const funkgui::Point p = centre(h.rect);
            r.host.move(p.x, p.y);
            r.host.tick(1, kDt);
            P.eq(std::string("footer.spec.") + h.row, b(startsWith(footerLine(r), h.prefix)), 1);
        }

        // The hovered slot's reason: the first slot whose resolved state is locked, derived or n/a with a reason.
        Rig r(key);
        const fcdsp::ParamView& view = r.ctx().frame.res.view;
        const L::SlotPlace* place = nullptr;
        const char* reason = nullptr;
        for (const L::SlotPlace& s : L::kSlots)
        {
            const fcdsp::ResolvedParam& rp = view.p[fcdsp::idx(s.pid)];
            const fcdsp::ParamSpec* spec = view.spec[fcdsp::idx(s.pid)];
            const bool refused = rp.state == fcdsp::SlotState::locked || rp.state == fcdsp::SlotState::derived
                              || rp.state == fcdsp::SlotState::na;
            if (refused && spec != nullptr && spec->reason != nullptr && spec->reason[0] != '\0')
            {
                place = &s;
                reason = spec->reason;
                break;
            }
        }
        if (place == nullptr)
        {
            P.note("footer_slot_reason", "\"no locked, derived or n/a slot in this Mode\"");
            return;
        }
        bool realGrid = false;                                   // U1s's grid lists sliders; the U1a stub staticText
        for (const funkgui::A11yItem& it : r.host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::slotGrid)
                && it.role == funkgui::A11yRole::slider)
                realGrid = true;
        const funkgui::SlotGeom g = L::slotGeom(*place);
        r.host.move(g.x + 10.0f, g.top + 20.0f);                 // inside the slot, clear of its attached word
        r.host.tick(2, kDt);
        if (!realGrid && r.ctx().hand.pid != place->pid)
        {
            P.note("footer_slot_reason", "\"the slot grid (U1a stub) offers no hand yet: checked once U1s merges\"");
            return;
        }
        const std::string_view want(reason);
        const std::string line = footerLine(r);
        P.eq("footer.slot_reason.hand", b(r.ctx().hand.pid == place->pid), 1);
        P.eq("footer.slot_reason.text", b(contains(line, want.substr(0, std::min<std::size_t>(want.size(), 40)))), 1);
        P.eq("display.hover_slot", b(startsWith(displayValue(r), view.spec[fcdsp::idx(place->pid)] != nullptr
                                                                   && view.spec[fcdsp::idx(place->pid)]->label != nullptr
                                                               ? view.spec[fcdsp::idx(place->pid)]->label
                                                               : place->label)), 1);
    }

    // ---- THEME ----------------------------------------------------------------------------------------------------------

    bool sameGeometry(const funkgui::Fingerprint& a, const funkgui::Fingerprint& c)
    {
        return a.geometry == c.geometry && a.text == c.text && a.statics == c.statics && a.texts == c.texts
            && a.rrects == c.rrects && a.segments == c.segments && a.areas == c.areas && a.tagCounts == c.tagCounts;
    }

    void theme(Probe& P, std::string_view key)
    {
        funkgui::UiPreferences& prefs = funkgui::UiPreferences::get();
        prefs.setTheme(0);
        funkgui::Fingerprint before;
        {
            Rig r(key, 0);
            before = funkgui::fingerprint(r.host.draw());
            const funkgui::Point paper = centre(L::footer::kThemeCells[1]);
            r.host.click(paper.x, paper.y);
            r.settle();
            P.eq("theme.select_paper", b(prefs.theme() == 1 && nothingWritten(r.facade)), 1);
            const std::vector<funkgui::A11yItem> items = r.host.accessibility();
            const funkgui::A11yItem* g = findItem(items, ui::ViewIndex::footer, funkgui::A11yRole::radioGroup, "Theme");
            P.eq("theme.a11y_value", b(g != nullptr && g->value == "Paper theme"), 1);
        }
        {
            Rig r(key, 1);                                       // the panel drawn in the theme the cell selected
            P.eq("theme.geometry_invariant", b(sameGeometry(before, funkgui::fingerprint(r.host.draw()))), 1);
            const funkgui::Point graphite = centre(L::footer::kThemeCells[0]);
            r.host.click(graphite.x, graphite.y);
            r.settle();
            P.eq("theme.select_graphite", b(prefs.theme() == 0 && nothingWritten(r.facade)), 1);
        }
    }

    // ---- notices --------------------------------------------------------------------------------------------------------

    void setKey(char (&out)[25], std::string_view k)
    {
        const std::size_t n = std::min(k.size(), sizeof out - 1);
        std::copy_n(k.data(), n, out);
        out[n] = '\0';
    }

    void notices(Probe& P, std::string_view key, const fcdsp::ModeDescriptor& desc)
    {
        const std::string name(desc.name);
        {
            Rig r(key, [](FakeFacade& f) {
                fcmp::StateNotice n;
                n.serial = 1;
                n.newerSession = true;
                f.setStateNotice(n);
            });
            P.eq("notice.newer", b(footerLine(r) == kNewer), 1);
            r.host.tick(static_cast<int>(10.1f / kDt), kDt);
            P.eq("notice.expires", b(footerLine(r) != kNewer), 1);
        }
        {
            Rig r(key, [key](FakeFacade& f) {
                fcmp::StateNotice n;
                n.serial = 2;
                n.modeMigrated = true;
                setKey(n.fromKey, "old-mode");
                setKey(n.toKey, key);
                f.setStateNotice(n);
            });
            P.eq("notice.migrated", b(footerLine(r) == "MODE 'OLD-MODE' IS UNKNOWN \xE2\x80\x94 LOADED '" + name + "'"), 1);
        }
        {
            Rig r(key, [key](FakeFacade& f) {
                fcmp::StateNotice n;
                n.serial = 3;
                n.modeRevised = true;
                n.savedRev = 1;
                n.currentRev = 2;
                setKey(n.toKey, key);
                f.setStateNotice(n);
            });
            P.eq("notice.revised", b(footerLine(r) == name + " UPDATED SINCE THIS SESSION (REV 1 \xE2\x86\x92 2)"), 1);
        }
        {   // a load while the editor is open
            Rig r(key);
            fcmp::StateNotice n;
            n.serial = 4;
            n.newerSession = true;
            r.facade.setStateNotice(n);
            r.host.tick(1, kDt);
            P.eq("notice.later_load", b(footerLine(r) == kNewer), 1);
        }
        {   // kUiPoisonReset: 3 s
            Rig r(key);
            fcdsp::UiFrame f = FakeFacade::quietFrame(r.ctx().frame.res.view.slot, r.ctx().frame.res.eng);
            f.flags |= fcdsp::kUiPoisonReset;
            r.facade.publish(f);
            r.host.tick(1, kDt);
            P.eq("notice.poison", b(footerLine(r) == kPoison), 1);
            r.host.tick(static_cast<int>(3.1f / kDt), kDt);
            P.eq("notice.poison_expires", b(footerLine(r) != kPoison), 1);
        }
    }

    // ---- the lookahead hint ---------------------------------------------------------------------------------------------

    void lookahead(Probe& P, std::string_view key, const fcdsp::ModeDescriptor& desc)
    {
        const std::string hint = std::string(desc.name)
            + " WITHOUT LOOKAHEAD CAN OVERSHOOT \xE2\x80\x94 SET LOOKAHEAD 5 MS ABOVE (+5 MS LATENCY)";
        const funkgui::Point band = centre(L::kBandHistory.plot);
        const funkgui::Point cells = centre(L::display::kLookaheadCells[1]);
        Rig r(key, [](FakeFacade& f) { f.setPlain(Pid::labudget, 0.0f); });
        r.host.move(band.x, band.y);
        r.host.tick(1, kDt);
        if (!desc.wantsLookahead)
        {
            P.eq("lookahead.none", b(!contains(footerLine(r), "WITHOUT LOOKAHEAD")), 1);
            return;
        }
        P.eq("lookahead.hint_band", b(footerLine(r) == hint), 1);
        r.host.move(cells.x, cells.y);
        r.host.tick(1, kDt);
        P.eq("lookahead.hint_cells", b(footerLine(r) == hint), 1);
        r.facade.setPlain(Pid::labudget, 1.0f);                  // a 5 ms budget: the configured budget follows
        r.host.move(band.x, band.y);
        r.host.tick(1, kDt);
        P.eq("lookahead.gone_with_budget", b(footerLine(r) != hint), 1);
        P.eq("lookahead.writes_nothing", b(nothingWritten(r.facade)), 1);
    }

    // ---- the GAIN REDUCTION readout -------------------------------------------------------------------------------------

    int liveValuePrims(const funkgui::PrimList& pl)
    {
        int n = 0;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == ui::tag::displayValue && (static_cast<uint32_t>(p.d2[3] + 0.5f) & funkgui::pflag::live) != 0)
                ++n;
        return n;
    }

    void display(Probe& P, std::string_view key)
    {
        Rig r(key);
        P.eq("display.not_live", b(displayValue(r) == "Gain reduction, no signal"), 1);
        P.eq("display.not_live_static", liveValuePrims(r.host.draw()), 0);
        fcdsp::UiFrame f = FakeFacade::quietFrame(r.ctx().frame.res.view.slot, r.ctx().frame.res.eng);
        f.flags |= fcdsp::kUiLive;
        f.appliedGrDb[0] = 4.2f;
        f.appliedGrDb[1] = 3.0f;
        f.inPeakDb[0] = f.inPeakDb[1] = -8.1f;
        f.outPeakDb[0] = f.outPeakDb[1] = -11.9f;
        r.facade.publish(f);
        r.host.tick(1, kDt);
        P.eq("display.gr_live", b(displayValue(r) == "Gain reduction, \xE2\x88\x92" "4.2 dB"), 1);
        P.ge("display.gr_live_tagged", liveValuePrims(r.host.draw()), 1);
        f.appliedGrDb[0] = f.appliedGrDb[1] = 0.0f;
        r.facade.publish(f);
        r.host.tick(1, kDt);
        P.eq("display.gr_zero", b(displayValue(r) == "Gain reduction, 0.0 dB"), 1);
        r.host.tick(static_cast<int>(0.6f / kDt), kDt);          // no publish for 0.6 s: stale
        P.eq("display.gr_stale", b(displayValue(r) == "Gain reduction, no signal"), 1);
    }

    // ---- Tab order ------------------------------------------------------------------------------------------------------

    void tabOrder(Probe& P, std::string_view key)
    {
        Rig r(key);
        const std::vector<funkgui::A11yItem> items = r.host.accessibility();
        const auto idOf = [&](ui::ViewIndex v, funkgui::A11yRole role, std::string_view title) -> uint32_t {
            const funkgui::A11yItem* it = findItem(items, v, role, title);
            return it != nullptr ? it->id : 0u;
        };
        const std::array<uint32_t, 6> chrome {
            idOf(ui::ViewIndex::header, funkgui::A11yRole::comboBox, "Mode"),
            idOf(ui::ViewIndex::displayRow, funkgui::A11yRole::radioGroup, "Quality"),
            idOf(ui::ViewIndex::displayRow, funkgui::A11yRole::radioGroup, "Lookahead budget"),
            idOf(ui::ViewIndex::displayRow, funkgui::A11yRole::toggleButton, "DELTA"),
            idOf(ui::ViewIndex::displayRow, funkgui::A11yRole::toggleButton, "BYPASS"),
            idOf(ui::ViewIndex::displayRow, funkgui::A11yRole::toggleButton, "CHARACTERISTICS"),
        };
        const uint32_t themeId = idOf(ui::ViewIndex::footer, funkgui::A11yRole::radioGroup, "Theme");
        std::vector<uint32_t> stops;
        for (int i = 0; i < 1024; ++i)
        {
            r.host.keys("tab");
            const uint32_t f = r.ctx().focus;
            if (!stops.empty() && f == stops.front())
                break;                                           // wrapped
            stops.push_back(f);
        }
        std::size_t k = 0;
        for (const uint32_t s : stops)
            if (k < chrome.size() && s == chrome[k])
                ++k;
        P.eq("tab.first_is_mode_latch", b(!stops.empty() && stops.front() == chrome[0] && chrome[0] != 0), 1);
        P.eq("tab.chrome_order", b(k == chrome.size()), 1);
        P.eq("tab.last_is_theme", b(!stops.empty() && stops.back() == themeId && themeId != 0), 1);
        r.host.keys("shift+tab");                                // from the Mode latch backwards: THEME
        P.eq("tab.shift_tab_reaches_theme", b(r.ctx().focus == themeId), 1);
    }
}

namespace
{
    // `-- --png-dir <dir>` (after the lone "--": the probe's own flags, S5 lead revision); "" when absent.
    std::string pngDir()
    {
        const int argc = *_NSGetArgc();
        char** argv = *_NSGetArgv();
        bool own = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a = argv[i] != nullptr ? argv[i] : "";
            if (a == "--")
                own = true;
            else if (own && a == "--png-dir" && i + 1 < argc && argv[i + 1] != nullptr)
                return argv[i + 1];
        }
        return {};
    }

    void pictures(Probe& P, std::string_view key, const std::string& dir)
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const auto save = [&](Rig& r, const char* what) {
            r.host.draw();
            const std::string path = dir + "/chrome-" + std::string(key) + "-" + what + ".png";
            if (!r.host.writePng(path.c_str(), 2))
                P.harnessError("ui.chrome: cannot write " + path);
            else
                std::printf("PNG      %s\n", path.c_str());
        };
        const auto liveFrame = [](Rig& r, float gr) {
            fcdsp::UiFrame f = FakeFacade::quietFrame(r.ctx().frame.res.view.slot, r.ctx().frame.res.eng);
            f.flags |= fcdsp::kUiLive;
            f.appliedGrDb[0] = gr;
            f.appliedGrDb[1] = gr * 0.8f;
            f.inPeakDb[0] = f.inPeakDb[1] = -8.1f;
            f.outPeakDb[0] = f.outPeakDb[1] = -11.9f;
            return f;
        };
        {   // hovering DELTA: its hover ease and its spec line; live GR on the display
            Rig r(key);
            r.facade.publish(liveFrame(r, 4.2f));
            const funkgui::Point p = centre(L::display::kDelta);
            r.host.move(p.x, p.y);
            r.host.tick(12, kDt);
            r.facade.publish(liveFrame(r, 4.2f));
            r.host.tick(1, kDt);
            save(r, "hover-delta");
        }
        {   // live GR, DELTA and BYPASS on, 5 MS budget, HQ
            Rig r(key);
            for (const Pid p : { Pid::delta, Pid::bypass })
                r.facade.setPlain(p, 1.0f);
            r.facade.setPlain(Pid::labudget, 1.0f);
            r.facade.setPlain(Pid::quality, 2.0f);
            r.facade.publish(liveFrame(r, 12.3f));
            r.host.tick(1, kDt);
            save(r, "live");
        }
        {   // Tab focus on QUALITY (the ring) and its spec line
            Rig r(key);
            r.host.keys("tab,tab");
            r.host.tick(1, kDt);
            save(r, "focus");
        }
        {   // after ›: mid-crossfade of the Mode texts, then settled with the summary on the footer
            Rig r(key);
            const funkgui::Point p = centre(L::header::kModeNext);
            r.host.click(p.x, p.y);
            r.host.tick(4, kDt);
            save(r, "fade");
            r.settle();
            save(r, "summary");
        }
        {   // a revised-Mode notice
            Rig r(key, [key](FakeFacade& f) {
                fcmp::StateNotice n;
                n.serial = 1;
                n.modeRevised = true;
                n.savedRev = 1;
                n.currentRev = 2;
                setKey(n.toKey, key);
                f.setStateNotice(n);
            });
            save(r, "notice");
        }
        {   // the bypass ramp at 40 %
            Rig r(key);
            fcdsp::UiFrame f = liveFrame(r, 3.0f);
            f.bypassAmt = 0.4f;
            r.facade.publish(f);
            r.host.tick(1, kDt);
            save(r, "bypass-ramp");
        }
        {   // the pointer over the band: the lookahead hint for a lookahead Mode, else nothing
            Rig r(key);
            const funkgui::Point p = centre(L::kBandHistory.plot);
            r.host.move(p.x, p.y);
            r.host.tick(1, kDt);
            save(r, "lookahead");
        }
        {   // CHARACTERISTICS on
            Rig r(key);
            const funkgui::Point p = centre(L::display::kCharacteristics);
            r.host.click(p.x, p.y);
            r.host.move(480.0f, 300.0f);
            r.settle();
            save(r, "chars");
        }
    }
}

FCMP_PROBE(ui, chrome)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.chrome: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.chrome: FCMP_PREFS_DIR is not set; THEME writes preferences, so the probe needs a sandbox "
                       "(CTest sets one)");
        return P.finish();
    }
    P.eq("font.ok", b(funkgui::FontService::get().atlas().baked() && funkgui::FontService::get().ok()), 1);
    const fcdsp::ModeDescriptor& desc = *entry->desc;
    modeLatch(P, C.key, desc);
    cells(P, C.key);
    latches(P, C.key);
    footer(P, C.key);
    theme(P, C.key);
    notices(P, C.key, desc);
    lookahead(P, C.key, desc);
    display(P, C.key);
    tabOrder(P, C.key);
    if (const std::string dir = pngDir(); !dir.empty())
        pictures(P, C.key, dir);
    return P.finish();
}
