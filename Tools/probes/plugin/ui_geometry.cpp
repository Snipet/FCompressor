// FCMP_PROBE layer=ui name=geometry scope=mode timeout=300
//
// ui.geometry.<key> (03 §3.6, C's G1; U1a): every view of fcmp::ui::views() renders for this Mode, headless, over a
// FakeFacade, at dpi 1 and 2 and in both themes.
//
// Per view × dpi (a fresh FakeFacade, Panel{skipHint, syncPreview} and HeadlessHost each time; setView(v, instant);
// settle() at 1/60 s, where more than 600 frames is a harness error, 03 §3.2.4):
//   golden rows   <view>.dpi<d>.*                 the theme-0 fingerprint (funkgui::addMetrics: geometry and text
//                                                 hashes exact, counts exact, extents abs 0.01, per-tag counts).
//                                                 Candidates only until the UI freeze FZ5 (K3 #21: golden.py adopt
//                                                 refuses ui.geometry rows before S12); golden_missing is expected.
//   spec rows     <view>.dpi<d>.theme_invariant   hash(theme 0) == hash(theme 1) (C G1)
//                 <view>.dpi<d>.glyphs_missing    0 missing glyphs
//                 <view>.dpi<d>.dump_roundtrip    dump v2 write -> parse gives the same primitives and fingerprint
//                 <view>.dpi<d>.view              the Panel shows the view's screen, tab and overlay
//                 <view>.dpi<d>.a11y_ids          every a11y id non-zero, unique, (subView << 16) | local
//   golden row    modebrowser.row.{x,y,w,h}       the Mode's own browser row (a listItem titled with the Mode's name)
//                                                 once the Mode browser lists rows (U5; the U1a stub has none)
// Once per Mode, spec rows for the skeleton the later cards build on:
//   skeleton.size, skeleton.views, skeleton.find.*   960×640; the six view ids in order (v1.2's settings the sixth);
//                                                     findView round trips
//   skeleton.batch.{facade,host}                     a composite write (tapMany) is one batch on the facade (K2 #23)
//   skeleton.teardown.{worker,gesture}               shutdown() stops the PreviewWorker, then closes a gesture (K2 #27)
//   skeleton.preview.{sync,async}                    PreviewWorker completes a request inline (syncPreview) and on its
//                                                    thread, and stop() joins it
//   skeleton.nav.*                                   a user crossfade settles; Esc leaves CHARACTERISTICS; a click
//                                                    outside an open browser closes it; UiState follows the screen
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Panel.h"
#include "editor/PreviewWorker.h"
#include "editor/SubView.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Fingerprint.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/text/FontService.h>

#include <funkgui/panel/HeadlessGuiScope.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;

    constexpr int   kMaxSettle = 600;
    constexpr float kDt = 1.0f / 60.0f;
    constexpr ui::PanelOptions kProbeOptions { true, true, false };   // skipHint, syncPreview, !ignoreLive

    struct Render
    {
        funkgui::Fingerprint fp;
        int      settle = 0;
        uint32_t missing = 0;
        bool     roundtrip = false;
        bool     viewOk = false;
        bool     idsOk = false;
        bool     rowFound = false;
        funkgui::Rect row{};
    };

    bool sameFingerprint(const funkgui::Fingerprint& a, const funkgui::Fingerprint& b)
    {
        return a.geometry == b.geometry && a.text == b.text && a.statics == b.statics && a.live == b.live
            && a.texts == b.texts && a.rrects == b.rrects && a.segments == b.segments && a.areas == b.areas
            && a.tagCounts == b.tagCounts;
    }

    // write -> parse through a real FILE*, then compare every primitive's bytes and the fingerprint.
    bool dumpRoundTrip(const funkgui::PrimList& pl, const funkgui::Fingerprint& fp)
    {
        std::FILE* f = std::tmpfile();
        if (f == nullptr)
            return false;
        bool ok = pl.writeText(f);
        std::string text;
        if (ok)
        {
            std::rewind(f);
            std::vector<char> buf(65536);
            std::size_t n = std::fread(buf.data(), 1, buf.size(), f);
            while (n > 0)
            {
                text.append(buf.data(), n);
                n = std::fread(buf.data(), 1, buf.size(), f);
            }
        }
        std::fclose(f);
        funkgui::PrimList back;
        if (!ok || !funkgui::PrimList::parseText(text, back) || back.prims.size() != pl.prims.size())
            return false;
        for (std::size_t i = 0; i < pl.prims.size(); ++i)
            if (std::memcmp(&back.prims[i], &pl.prims[i], sizeof(funkgui::Prim)) != 0)
                return false;
        return sameFingerprint(funkgui::fingerprint(back), fp);
    }

    bool idsValid(const std::vector<funkgui::A11yItem>& items)
    {
        std::set<uint32_t> seen;
        for (const funkgui::A11yItem& it : items)
        {
            const int v = ui::viewIndexOf(it.id);
            if (it.id == 0 || (it.id & 0xFFFFu) == 0 || v < 0 || v >= ui::kSubViewCount || !seen.insert(it.id).second)
                return false;
        }
        return true;
    }

    Render render(const ui::ViewSpec& v, const fcdsp::ModeDescriptor& desc, float dpi, int theme)
    {
        Render r;
        fcmp::probe::FakeFacade facade(desc.key);
        ui::Panel panel(facade, kProbeOptions);
        {
            funkgui::HeadlessHost host(panel, theme, dpi);
            panel.setView(v, true);
            r.settle = host.settle(kMaxSettle, kDt);
            const funkgui::PrimList& pl = host.draw();
            r.fp = funkgui::fingerprint(pl);
            r.missing = pl.missingGlyphs;
            r.roundtrip = dumpRoundTrip(pl, r.fp);
            r.viewOk = panel.screen() == v.screen && panel.scTab() == v.tab && panel.overlay() == v.overlay;
            const std::vector<funkgui::A11yItem> items = host.accessibility();
            r.idsOk = idsValid(items);
            for (const funkgui::A11yItem& it : items)
                if (it.visible && it.role == funkgui::A11yRole::listItem && it.title == std::string(desc.name))
                {
                    r.rowFound = true;
                    r.row = it.bounds;
                }
        }
        return r;
    }

    void renderViews(Probe& P, const fcdsp::ModeDescriptor& desc)
    {
        for (const ui::ViewSpec& v : ui::views())
            for (const int dpi : { 1, 2 })
            {
                const std::string prefix = std::string(v.id) + ".dpi" + std::to_string(dpi);
                const Render r0 = render(v, desc, static_cast<float>(dpi), 0);
                const Render r1 = render(v, desc, static_cast<float>(dpi), 1);
                if (r0.settle > kMaxSettle || r1.settle > kMaxSettle)
                    P.harnessError(prefix + ": the panel did not settle within 600 frames (an unsettled ease)");
                P.eq(prefix + ".theme_invariant", sameFingerprint(r0.fp, r1.fp) ? 1 : 0, 1);
                P.eq(prefix + ".glyphs_missing", r0.missing, 0);
                P.eq(prefix + ".dump_roundtrip", r0.roundtrip ? 1 : 0, 1);
                P.eq(prefix + ".view", r0.viewOk ? 1 : 0, 1);
                P.eq(prefix + ".a11y_ids", r0.idsOk ? 1 : 0, 1);
                funkgui::addMetrics(P, prefix, r0.fp);
                if (v.overlay == ui::Overlay::modeBrowser && dpi == 1 && r0.rowFound)
                {
                    P.num("modebrowser.row.x", r0.row.x, funkgui::test::Tol::abs(0.01));
                    P.num("modebrowser.row.y", r0.row.y, funkgui::test::Tol::abs(0.01));
                    P.num("modebrowser.row.w", r0.row.w, funkgui::test::Tol::abs(0.01));
                    P.num("modebrowser.row.h", r0.row.h, funkgui::test::Tol::abs(0.01));
                }
            }
    }

    void skeleton(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const fcdsp::ModeDescriptor& desc = *entry.desc;
        P.eq("skeleton.views", static_cast<int64_t>(ui::views().size()), 6);
        constexpr std::array<std::string_view, 6> kIds { "panel", "chars.sidechain", "chars.colour", "modebrowser",
                                                          "presetbrowser", "settings" };   // settings: v1.2 (ADR-85)
        for (std::size_t i = 0; i < kIds.size() && i < ui::views().size(); ++i)
        {
            const ui::ViewSpec& v = ui::views()[i];
            P.eq("skeleton.find." + std::string(kIds[i]), v.id == kIds[i] && ui::findView(kIds[i]) == &v ? 1 : 0, 1);
        }
        P.eq("skeleton.find.unknown", ui::findView("nope") == nullptr ? 1 : 0, 1);

        {   // size, batches, teardown
            fcmp::probe::FakeFacade facade(desc.key);
            ui::Panel panel(facade, { true, false, false });
            P.eq("skeleton.size", panel.width() == 960 && panel.height() == 640 ? 1 : 0, 1);
            funkgui::HeadlessHost host(panel, 0, 2.0f);
            funkgui::GestureController& g = *panel.context().gestures;
            const float thr = facade.fakePort(fcdsp::Pid::thr).value01();
            const float mix = facade.fakePort(fcdsp::Pid::mix).value01();
            const std::array<std::pair<funkgui::ParamPort*, float>, 2> writes { {
                { &facade.port(fcdsp::Pid::thr), thr < 0.5f ? thr + 0.25f : thr - 0.25f },
                { &facade.port(fcdsp::Pid::mix), mix < 0.5f ? mix + 0.25f : mix - 0.25f } } };
            g.tapMany(writes);
            bool batched = facade.writes().size() == 2;
            for (const fcmp::probe::FakeWrite& w : facade.writes())
                batched = batched && w.batchDepth == 1 && w.inGesture;
            P.eq("skeleton.batch.facade", batched && facade.batches() == 1 && facade.batchDepth() == 0 ? 1 : 0, 1);
            P.eq("skeleton.batch.host", host.log.batches == 1 && host.log.batchDepth == 0 ? 1 : 0, 1);

            fcmp::probe::FakePort& port = facade.fakePort(fcdsp::Pid::thr);
            port.resetCounts();
            g.beginDrag(port);
            panel.shutdown();
            P.eq("skeleton.teardown.worker", panel.context().preview.stopped() ? 1 : 0, 1);
            P.eq("skeleton.teardown.gesture", port.begins() == 1 && port.ends() == 1 && !port.inGesture() ? 1 : 0, 1);
            P.eq("skeleton.teardown.flag", panel.isShutDown() ? 1 : 0, 1);
        }

        {   // PreviewWorker: inline and on its thread
            fcdsp::Resolution res;
            fcdsp::RawParams raw;
            for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
                raw.v[i] = fcdsp::kHostParams[i].def;
            raw.modeSlot = static_cast<uint8_t>(fcdsp::slotOf(entry));
            fcdsp::resolve(entry, raw, res);

            ui::PreviewWorker sync(true);
            sync.setActive(true);
            sync.request(entry, res.eng, 48000.0f, 42);
            const bool queued = sync.pending();
            sync.tick(kDt);
            P.eq("skeleton.preview.sync", queued && !sync.pending() && sync.result().key == 42
                                              && sync.result().serial == 1 ? 1 : 0, 1);

            ui::PreviewWorker async(false);
            async.setActive(true);
            async.request(entry, res.eng, 48000.0f, 43);
            int ticks = 0;
            for (; ticks < 2000 && (async.pending() || async.result().key != 43); ++ticks)
            {
                async.tick(kDt);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            P.eq("skeleton.preview.async", async.result().key == 43 && !async.pending() ? 1 : 0, 1);
            async.stop();
            P.eq("skeleton.preview.stop", async.stopped() && !async.pending() ? 1 : 0, 1);
        }

        {   // navigation through the composer
            fcmp::probe::FakeFacade facade(desc.key);
            ui::Panel panel(facade, kProbeOptions);
            funkgui::HeadlessHost host(panel, 0, 2.0f);
            host.settle(kMaxSettle, kDt);
            panel.setView({ nullptr, ui::Screen::characteristics, fcmp::ScTab::sidechain, ui::Overlay::none }, false);
            const bool fading = panel.wantsFullRate();
            const int frames = host.settle(kMaxSettle, kDt);
            P.eq("skeleton.nav.crossfade", fading && frames > 0 && frames <= kMaxSettle
                                                && panel.screen() == ui::Screen::characteristics
                                                && facade.uiState().charExpanded ? 1 : 0, 1);
            host.keys("escape");
            host.settle(kMaxSettle, kDt);
            P.eq("skeleton.nav.escape", panel.screen() == ui::Screen::panel && !facade.uiState().charExpanded ? 1 : 0, 1);
            panel.setView(ui::views()[3], false);
            host.settle(kMaxSettle, kDt);
            const bool open = panel.overlay() == ui::Overlay::modeBrowser;
            host.click(480.0f, 470.0f);                           // a slot row: outside the overlay
            host.settle(kMaxSettle, kDt);
            P.eq("skeleton.nav.click_outside", open && panel.overlay() == ui::Overlay::none ? 1 : 0, 1);
            panel.setView(ui::views()[2], false);
            host.settle(kMaxSettle, kDt);
            P.eq("skeleton.nav.tab", panel.scTab() == fcmp::ScTab::colour
                                         && facade.uiState().scTab == fcmp::ScTab::colour ? 1 : 0, 1);
            host.click(760.0f, 412.0f);                           // the SIDECHAIN tab cell
            host.settle(kMaxSettle, kDt);
            P.eq("skeleton.nav.tab_click", panel.scTab() == fcmp::ScTab::sidechain ? 1 : 0, 1);
        }
    }
}

FCMP_PROBE(ui, geometry)
{
    const funkgui::HeadlessGuiScope gui;                          // FontService bakes the atlas through JUCE's fonts
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.geometry: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    const bool baked = funkgui::FontService::get().atlas().baked();   // atlas() bakes on the first call
    P.eq("font.ok", baked && funkgui::FontService::get().ok() ? 1 : 0, 1);
    skeleton(P, *entry);
    renderViews(P, *entry->desc);
    return P.finish();
}
