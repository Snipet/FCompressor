// FCMP_PROBE layer=ui name=nolive scope=mode timeout=300
//
// ui.nolive.<key>: FCMP_UI_NO_LIVE (PanelOptions::ignoreLive) makes the drawn frame independent of the telemetry, the
// precondition of live parity (ui.live, Scripts/gui-live.sh; 03 §3.6, 02 §5.1). gui-live compares the real Standalone —
// a real processor on whatever audio device the machine has, publishing UiFrames and history columns — with ui.dump's
// headless frame, which has no telemetry at all. Spec rows only.
//
// For each of gui-live's five views (panel, chars.sidechain, chars.colour, modebrowser, presetbrowser):
// - the reference is ui.dump's frame: a FakeFacade that publishes nothing, Panel{skipHint, syncPreview}, HeadlessHost at
//   dpi 2, theme 0, setView(instant), settle at 1/60 s, draw;
// - each variant is gui-live's live side: Panel{skipHint, syncPreview, ignoreLive} over a FakeFacade that, before the
//   ticks its script names, publishes a busy UiFrame (kUiLive, attack/release phases, GR on both lanes, levels, SC and
//   colour peaks, the external key, stage 2, auto-slow, range-limited, output over, effective times, crest, internals,
//   a latency) and pushes 16–17 history columns, as a processor with signal does at 60 frames per second. The Panel is
//   ticked exactly as EditorHost ticks it for FCMP_CANVAS_DUMP_AFTER = max(8, reference settle + 2) (gui-live's value;
//   the dumped frame is drawn after dumpAfter + 1 ticks), then drawn.
//   <view>.<variant>.geometry   the frame's fingerprint geometry hash equals the reference's
//   <view>.<variant>.text       ... and its text hash
//   <view>.<variant>.settled    the Panel does not want full rate at the dumped frame (it drew a settled state)
// Variants: fs48k (every tick at 48 kHz, the default), fs24k (24 kHz: a Bluetooth headset in its hands-free profile,
// which it enters when the Standalone opens its microphone), fs44k1 (44.1 kHz), fs96k (96 kHz), switch (48 kHz, then
// 24 kHz from half the capture, dumpAfter / 2: the headset changing profile mid-capture, inside every view's window,
// the panel's 8 ticks too) and late (no frame until the last tick before the dump, then 24 kHz: a device that starts
// late).
// SidechainPlot and StepPlot once took UiFrame::sampleRate even under ignoreLive, so gui-live's chars views differed in
// geometry whenever the Standalone's device did not run at 48 kHz at the dumped frame (telemetry::sampleRate): with a
// Bluetooth headset as the default device, at 24 kHz (fs24k reproduced the failing live hashes exactly).
//
// The probe needs FCMP_PREFS_DIR (CTest sets a sandbox): the panel reads UiPreferences.
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/Panel.h"
#include "editor/SubView.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Fingerprint.h>
#include <funkgui/panel/HeadlessHost.h>

#include <funkgui/panel/HeadlessGuiScope.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    using fcmp::probe::FakeFacade;

    constexpr float kDt = 1.0f / 60.0f;                           // ui.dump's settle dt; gui-live's FCMP_UI_FIXED_DT
    constexpr int   kMaxSettle = 600;
    constexpr int   kMinDumpAfter = 8;                            // gui-live: max(8, settle + 2)
    constexpr ui::PanelOptions kReference { true, true, false };  // skipHint, syncPreview (ui.dump)
    constexpr ui::PanelOptions kNoLive    { true, true, true };   // + ignoreLive (FCMP_UI_NO_LIVE=1)
    constexpr std::array<std::string_view, 5> kViews { "panel", "chars.sidechain", "chars.colour", "modebrowser",
                                                       "presetbrowser" };   // gui-live.sh's VIEWS

    int b(bool v) { return v ? 1 : 0; }

    // What the facade holds before tick `i` of a variant: nothing (fs 0) or a busy frame at fs.
    struct Variant
    {
        const char* name;
        float (*fsAt)(int tick, int dumpAfter);
    };

    constexpr std::array<Variant, 6> kVariants {{
        { "fs48k",  [](int, int) { return 48000.0f; } },
        { "fs24k",  [](int, int) { return 24000.0f; } },
        { "fs44k1", [](int, int) { return 44100.0f; } },
        { "fs96k",  [](int, int) { return 96000.0f; } },
        { "switch", [](int i, int after) { return i < after / 2 ? 48000.0f : 24000.0f; } },
        { "late",   [](int i, int after) { return i < after ? 0.0f : 24000.0f; } },
    }};

    // A processor with signal: every block flag a real engine can raise, both lanes compressing.
    fcdsp::UiFrame busyFrame(const ui::PanelContext& ctx, float fs, int tick)
    {
        const auto slot = static_cast<uint16_t>(ctx.frame.res.view.slot);
        fcdsp::UiFrame f = FakeFacade::quietFrame(slot, ctx.frame.res.eng);
        const float gr = 4.0f + 2.0f * static_cast<float>(tick % 5);
        f.sampleRate = fs;
        f.latencySamples = 64;
        f.flags = fcdsp::kUiLive | fcdsp::kUiExtKeyActive | fcdsp::kUiS2Active | fcdsp::kUiAutoSlow
                | fcdsp::kUiRangeLimited | fcdsp::kUiOutOver | (1u << 16) | (3u << 18);   // lane 0 attack, 1 release
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto k = static_cast<std::size_t>(ch);
            f.inPeakDb[k] = -9.0f;
            f.inRmsDb[k] = -14.0f;
            f.outPeakDb[k] = -9.0f - gr;
            f.outRmsDb[k] = -14.0f - gr;
            f.scPeakDb[k] = -12.0f;
            f.colourInPeakDb[k] = -6.0f;
            f.curveXDb[k] = -9.0f;
            f.targetGrDb[k] = gr + 1.0f;
            f.appliedGrDb[k] = gr - static_cast<float>(ch);
            f.blockMaxGrDb[k] = gr + 0.5f;
            f.s2GrDb[k] = 1.5f;
            f.attackNowMs[k] = 3.0f;
            f.releaseNowMs[k] = 180.0f;
            f.crestDb[k] = 9.0f;
        }
        for (float& v : f.internals)
            v = 0.5f;
        return f;
    }

    // 16–17 one-millisecond columns per frame, as the engine writes them at 60 frames per second.
    void pushColumns(FakeFacade& facade, const ui::PanelContext& ctx, int tick)
    {
        const auto slot = static_cast<uint32_t>(ctx.frame.res.view.slot);
        const int n = tick % 3 == 2 ? 16 : 17;
        for (int i = 0; i < n; ++i)
        {
            fcdsp::HistoryColumn c{};
            c.inPeakDb = -9.0f;
            c.outPeakDb = -15.0f;
            c.detMaxDb = -9.0f;
            c.grMaxDb = 6.0f + static_cast<float>(i % 4);
            c.grMinDb = 5.0f;
            c.tgtMaxDb = 7.0f;
            c.internal0 = 0.5f;
            c.bits = 1u | (1u << 2) | (1u << 4) | (slot << 8);
            facade.pushColumn(c);
        }
    }

    struct Frame
    {
        funkgui::Fingerprint fp;
        int  frames = 0;                                          // settle frames (reference) or ticks (variant)
        bool settled = false;
    };

    Frame reference(const fcdsp::ModeEntry& entry, const ui::ViewSpec& view)
    {
        FakeFacade facade(entry.desc->key);
        ui::Panel panel(facade, kReference);
        funkgui::HeadlessHost host(panel, 0, 2.0f);
        panel.setView(view, true);
        Frame r;
        r.frames = host.settle(kMaxSettle, kDt);
        r.settled = r.frames <= kMaxSettle;
        r.fp = funkgui::fingerprint(host.draw());
        return r;
    }

    Frame live(const fcdsp::ModeEntry& entry, const ui::ViewSpec& view, const Variant& v, int dumpAfter)
    {
        FakeFacade facade(entry.desc->key);
        ui::Panel panel(facade, kNoLive);
        funkgui::HeadlessHost host(panel, 0, 2.0f);
        panel.setView(view, true);
        Frame r;
        for (int i = 0; i <= dumpAfter; ++i, ++r.frames)          // EditorHost: frame n is drawn after n + 1 ticks
        {
            if (const float fs = v.fsAt(i, dumpAfter); fs > 0.0f)
            {
                pushColumns(facade, panel.context(), i);
                facade.publish(busyFrame(panel.context(), fs, i));
            }
            host.tick(1, kDt);
        }
        r.settled = !panel.wantsFullRate();
        r.fp = funkgui::fingerprint(host.draw());
        return r;
    }

    void run(Probe& P, const fcdsp::ModeEntry& entry)
    {
        for (const std::string_view id : kViews)
        {
            const ui::ViewSpec* view = ui::findView(id);
            if (view == nullptr)
            {
                P.harnessError("ui.nolive: no view '" + std::string(id) + "'");
                continue;
            }
            const Frame ref = reference(entry, *view);
            const std::string k(id);
            P.le(k + ".reference_settles", ref.frames, kMaxSettle);
            const int dumpAfter = std::max(kMinDumpAfter, ref.frames + 2);
            for (const Variant& v : kVariants)
            {
                const Frame got = live(entry, *view, v, dumpAfter);
                const std::string kv = k + "." + v.name;
                if (got.fp.geometry != ref.fp.geometry || got.fp.text != ref.fp.text)
                    std::printf("NOTE     %s: geometry %016llx text %016llx, reference %016llx %016llx\n", kv.c_str(),
                                static_cast<unsigned long long>(got.fp.geometry),
                                static_cast<unsigned long long>(got.fp.text),
                                static_cast<unsigned long long>(ref.fp.geometry),
                                static_cast<unsigned long long>(ref.fp.text));
                P.eq(kv + ".geometry", b(got.fp.geometry == ref.fp.geometry), 1);
                P.eq(kv + ".text", b(got.fp.text == ref.fp.text), 1);
                P.eq(kv + ".settled", b(got.settled), 1);
            }
        }
    }
}

FCMP_PROBE(ui, nolive)
{
    const funkgui::HeadlessGuiScope gui;                          // FontService bakes the atlas through JUCE's fonts
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.nolive reads UiPreferences: set FCMP_PREFS_DIR to a sandbox (CTest does)");
        return P.finish();
    }
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.nolive: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    run(P, *entry);
    return P.finish();
}
