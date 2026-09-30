// FCMP_PROBE layer=ui name=live_stale scope=mode timeout=300
//
// ui.live_stale.<key> (UF1a, S11; ADR-69, ADR-70): the panel when the audio stops. A FakeFacade feeds 2 s of scripted
// live frames at 60 Hz with their history columns (16–17 per frame, 1 ms each; input −24 dBFS; a moving GR: bursts of
// 3–12 dB every 0.4 s released with τ 120 ms), then nothing for 2 s — a host that stopped calling processBlock — while
// the HeadlessHost keeps ticking at 60 Hz (Panel{skipHint, syncPreview}; dpi 2, theme 0). Spec rows:
//   feed.stale                 after the 2 s the feed is stale, a frame having been seen
//   ink.static_same            every static (non-live) primitive of the last live frame that a stale frame draws with the
//                              same tag, kind and geometry has the same colours, on every stale frame: no control, label,
//                              caption, chrome or curve dims (the count of differences is 0)
//   ink.static_matched         and >= 90 % of the last live frame's static primitives are drawn again unchanged
//   history.advances           once stale (0.5 s after the last frame) the newest HISTORY data column moves left on every
//                              frame, by that frame's time at the plot's px/ms (wall clock), ±0.15 px
//   history.advances_px        over the whole stale part, (2.0 − 0.5) s × px/ms, ±2 px
//   history.gap                a GAP run reaches from the newest data to the plot's right edge
//   meters.floor               at the end no METER_IN, METER_OUT, METER_GR or METER_HOLD primitive is drawn
//   readout.zero               at the end the GAIN REDUCTION readout reads "0.0 dB" and its sub "IN −∞ · OUT −∞"
//   readout.never_dash         after the first frame the readout never reads "no signal" and no band meter's a11y value
//                              is "–"
//   bar.zero                   at the end the display row's GR bar is empty
//   dot.held_fresh             OP_DOT is drawn at full alpha 0.25 s after the last frame (the feed is still fresh)
//   dot.fading                 0.75 s after it, OP_DOT is drawn at 0 < alpha < 255 (fading out over 0.4 s where it was)
//   dot.gone                   1.0 s after it, no OP_DOT, GR_NEEDLE, TARGET_DOT or OP_TRAIL is drawn
//   readout.hold_db            during the live part, at every refresh the readout equals the max GR of the script over
//                              the store's newest 1000 columns and the newest frame, <= 0.1 dB
//   readout.refreshes          ... it refreshed 4 times per second (>= 7 in the 2 s), and readout.quantised: its text
//                              never changed between two refreshes
//   rate.moving                at the end of the stale part (data still in view) the panel wants full rate
//   rate.idle                  6 s later (the data scrolled out, meters at the floor, the dot gone) it does not
//   rate.hover                 a pointer move over the band brings full rate back at once ...
//   rate.hover_ends            ... for layout::live::kActiveS; 0.2 s after that the panel is idle again
//   chars.rest                 on chars.sidechain at the end, READOUTS DET reads "−∞", TARGET and APPLIED "0.0", PHASE
//                              "IDLE" (the frame at rest), and no row reads "–" but an n/a S2 GR
//   hidden_gap.<plot>.*        (S13 H1a) the band's HISTORY and CONTROL PATH: 1 s of audio, then the other screen for
//                              1.5 s without a frame and 1 s of audio again, then back: the plot draws the same GAP runs
//                              (±1 px) as the same plot left in view all along (.same), which draws at least one
//                              (.reference); the stop happened while it was hidden, and is still a gap
//                              (HistoryPlot::keepTime)
//
// Pictures (not a test): probe-own flags after "--": -- --png-dir <dir> writes <dir>/stale-<key>-live.png (the last
// live frame), <dir>/stale-<key>-<ms>.png at 250, 500, 750, 1000, 1500 and 2000 ms after it (the stopped transport),
// <dir>/gr-<key>-<ms>.png at every GAIN REDUCTION refresh of the live part (the readout under a moving GR) and
// <dir>/stale-<key>-chars.png (the Characteristics screen at rest).
//
// The probe needs FCMP_PREFS_DIR (CTest sets a sandbox): the panel reads the span and scale preferences.
#include "ProbeRegistry.h"

#include "FakeFacade.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SubView.h"
#include "editor/Tags.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/panel/HeadlessHost.h>
#include <funkgui/prefs/UiPreferences.h>
#include <funkgui/text/FontService.h>

#include <juce_gui_basics/juce_gui_basics.h>


#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace L = fcmp::ui::layout;
    using fcmp::probe::FakeFacade;

    constexpr float kDt = 1.0f / 60.0f;
    constexpr int   kLiveFrames = 120;                            // 2 s
    constexpr int   kStaleFrames = 120;                           // 2 s
    constexpr float kInDb = -24.0f;
    constexpr ui::PanelOptions kOpts { true, true, false };       // skipHint, syncPreview, live
    constexpr const char* kMinusInf = "\xE2\x88\x92\xE2\x88\x9E";
    constexpr const char* kDash = "\xE2\x80\x93";

    int b(bool v) { return v ? 1 : 0; }

    // ---- the script ---------------------------------------------------------------------------------------------------

    // GR at time t ms: a burst every 400 ms (peaks 9, 4, 12, 6, 3 dB), reached in 5 ms, released with τ 120 ms.
    float scriptGr(double tMs)
    {
        constexpr std::array<float, 5> peaks { 9.0f, 4.0f, 12.0f, 6.0f, 3.0f };
        const double period = 400.0;
        const auto k = static_cast<int64_t>(std::floor(tMs / period));
        const double u = tMs - static_cast<double>(k) * period;
        const float p = peaks[static_cast<std::size_t>(k % static_cast<int64_t>(peaks.size()))];
        if (u < 5.0)
            return p * static_cast<float>(u / 5.0);
        return p * static_cast<float>(std::exp(-(u - 5.0) / 120.0));
    }

    struct Script
    {
        FakeFacade&    facade;
        fcdsp::UiFrame base;
        uint8_t        slot = 0;
        double         tMs = 0.0;                                 // the next column's start
        std::vector<float> columnGr;                               // every column pushed, oldest first
        float          lastApplied = 0.0f, lastBlockMax = 0.0f;

        void frame(int index)
        {
            const int n = index % 3 == 2 ? 16 : 17;               // 16.67 columns per frame on average
            float blockMax = 0.0f;
            for (int i = 0; i < n; ++i, tMs += 1.0)
            {
                const float gr = scriptGr(tMs);
                fcdsp::HistoryColumn c{};
                c.inPeakDb = kInDb + 0.5f * static_cast<float>((static_cast<int64_t>(tMs) / 7) % 3);
                c.outPeakDb = c.inPeakDb - gr;
                c.detMaxDb = c.inPeakDb;
                c.grMaxDb = gr;
                c.grMinDb = gr * 0.9f;
                c.tgtMaxDb = gr * 1.1f;
                c.bits = (gr > 0.5f ? 3u : 0u) | (static_cast<uint32_t>(slot) << 8);
                facade.pushColumn(c);
                columnGr.push_back(gr);
                blockMax = std::max(blockMax, gr);
            }
            fcdsp::UiFrame f = base;
            f.flags |= fcdsp::kUiLive | (3u << 16);
            const float gr = scriptGr(tMs);
            f.appliedGrDb[0] = gr;
            f.appliedGrDb[1] = 0.8f * gr;
            f.blockMaxGrDb[0] = std::max(blockMax, gr);
            f.blockMaxGrDb[1] = 0.8f * f.blockMaxGrDb[0];
            f.targetGrDb[0] = f.targetGrDb[1] = 1.1f * gr;
            f.inPeakDb[0] = f.inPeakDb[1] = kInDb;
            f.inRmsDb[0] = f.inRmsDb[1] = kInDb - 3.0f;
            f.outPeakDb[0] = f.outPeakDb[1] = kInDb - gr;
            f.outRmsDb[0] = f.outRmsDb[1] = kInDb - gr - 3.0f;
            f.curveXDb[0] = f.curveXDb[1] = kInDb;
            facade.publish(f);
            lastApplied = gr;
            lastBlockMax = f.blockMaxGrDb[0];
        }

        // The readout ADR-70 prescribes now: the max over the newest 1000 columns and the newest frame.
        float holdDb() const
        {
            float m = std::max(lastApplied, lastBlockMax);
            const std::size_t n = columnGr.size();
            for (std::size_t i = n > 1000 ? n - 1000 : 0; i < n; ++i)
                m = std::max(m, columnGr[i]);
            return m > L::display::kGrShownDb ? m : 0.0f;
        }
    };

    // ---- reading the frame ---------------------------------------------------------------------------------------------

    std::vector<const funkgui::Prim*> tagged(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        std::vector<const funkgui::Prim*> v;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == t)
                v.push_back(&p);
        return v;
    }

    bool isLive(const funkgui::Prim& p) { return (static_cast<uint32_t>(p.d2[3] + 0.5f) & funkgui::pflag::live) != 0; }

    funkgui::PrimKind kindOf(const funkgui::Prim& p)
    {
        return static_cast<funkgui::PrimKind>(static_cast<int>(p.d2[2] + 0.5f));
    }

    // A filled disc's alpha (c0 is the fill; c1 the ring, opaque black when there is none).
    uint32_t alphaOf(const funkgui::Prim& p) { return p.c0 >> 24; }

    // A static primitive's identity: tag, kind, quad and the first varyings (a glyph's atlas cell), quantised to 1/64 px.
    using Key = std::tuple<uint32_t, int, long, long, long, long, long, long>;

    Key keyOf(const funkgui::Prim& p)
    {
        const auto q = [](float v) { return std::lround(static_cast<double>(v) * 64.0); };
        return { static_cast<uint32_t>(p.tag), static_cast<int>(kindOf(p)), q(p.x0), q(p.y0), q(p.x1), q(p.y1),
                 q(p.d0[0]), q(p.d0[1]) };
    }

    std::map<Key, std::pair<uint32_t, uint32_t>> staticInks(const funkgui::PrimList& pl)
    {
        std::map<Key, std::pair<uint32_t, uint32_t>> m;
        for (const funkgui::Prim& p : pl.prims)
            if (!isLive(p))
                m[keyOf(p)] = { p.c0, p.c1 };
        return m;
    }

    // The right end of the newest HISTORY data column (HIST_IN); -1 without one.
    float dataEnd(const funkgui::PrimList& pl)
    {
        float x = -1.0f;
        for (const funkgui::Prim* p : tagged(pl, ui::tag::histIn))
            x = std::max(x, p->x1);
        return x;
    }

    std::string displayItem(const funkgui::HeadlessHost& host, bool description)
    {
        for (const funkgui::A11yItem& it : host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::displayRow) && it.title == "Display")
                return description ? it.description : it.value;
        return "<no item>";
    }

    // "Gain reduction, −4.2 dB" -> 4.2; NaN for anything else.
    float displayGr(const std::string& v)
    {
        constexpr std::string_view head = "Gain reduction, ";
        if (std::string_view(v).substr(0, head.size()) != head)
            return std::nanf("");
        std::string num = v.substr(head.size());
        const std::string minus = "\xE2\x88\x92";
        bool negative = false;
        if (num.compare(0, minus.size(), minus) == 0)
        {
            negative = true;
            num.erase(0, minus.size());
        }
        char* end = nullptr;
        const float x = std::strtof(num.c_str(), &end);
        if (end == num.c_str() || std::string_view(end) != " dB")
            return std::nanf("");
        return negative ? x : -x;
    }

    bool bandMeterDash(const funkgui::HeadlessHost& host)
    {
        for (const funkgui::A11yItem& it : host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::band) && it.role == funkgui::A11yRole::progressBar
                && it.visible && it.value == kDash)
                return true;
        return false;
    }

    // ---- pictures ------------------------------------------------------------------------------------------------------

    std::string pngDir()
    {
        const int argc = fcmp::probe::argc();
        char** argv = fcmp::probe::argv();
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

    void png(Probe& P, funkgui::HeadlessHost& host, const std::string& dir, const std::string& name)
    {
        if (dir.empty())
            return;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        host.draw();
        const std::string path = dir + "/" + name + ".png";
        if (!host.writePng(path.c_str(), 2))
            P.harnessError("ui.live_stale: cannot write " + path);
        else
            std::printf("PNG      %s\n", path.c_str());
    }

    std::string ms4(double s)
    {
        char t[16];
        std::snprintf(t, sizeof t, "%04ld", std::lround(s * 1000.0));
        return t;
    }

    // ---- the run -------------------------------------------------------------------------------------------------------

    void run(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const fcdsp::ModeDescriptor& desc = *entry.desc;
        const std::string key(desc.key);
        const std::string dir = pngDir();
        FakeFacade facade(desc.key);
        ui::Panel panel(facade, kOpts);
        funkgui::HeadlessHost host(panel, 0, 2.0f);
        host.settle(600, kDt);
        const ui::PanelContext& ctx = panel.context();
        const auto slot0 = static_cast<uint8_t>(ctx.frame.res.view.slot);
        Script s{ facade, FakeFacade::quietFrame(slot0, ctx.frame.res.eng), slot0, 0.0, {}, 0.0f, 0.0f };

        // ---- live: 2 s, the readout at every refresh ------------------------------------------------------------------
        const auto slotNow = [&]() {
            return static_cast<int64_t>(std::floor(ctx.seconds / static_cast<double>(L::display::kGrRefreshS)));
        };
        int64_t lastSlot = slotNow();
        bool seen = false;
        std::string lastText;
        double worst = 0.0;
        int refreshes = 0, between = 0;
        for (int k = 0; k < kLiveFrames; ++k)
        {
            s.frame(k);
            host.tick(1, kDt);
            const std::string v = displayItem(host, false);
            const float shown = displayGr(v);
            const int64_t slot = slotNow();
            if (!std::isnan(shown) && (!seen || slot != lastSlot))
            {
                worst = std::max(worst, static_cast<double>(std::fabs(shown - s.holdDb())));
                ++refreshes;
                png(P, host, dir, "gr-" + key + "-" + ms4(static_cast<double>(k + 1) * static_cast<double>(kDt)));
            }
            else if (seen && v != lastText)
            {
                ++between;
            }
            seen = seen || !std::isnan(shown);
            lastText = v;
            lastSlot = slot;
        }
        std::printf("NOTE     ui.live_stale: %d refreshes in 2 s, worst %.3f dB from the script's 1 s max, %d changes "
                    "between refreshes\n", refreshes, worst, between);
        P.le("readout.hold_db", refreshes > 0 ? worst : 99.0, 0.1);
        P.ge("readout.refreshes", refreshes, 7);
        P.eq("readout.quantised", between, 0);

        // ---- the last live frame: static inks, the dot ----------------------------------------------------------------
        const funkgui::PrimList& livePl = host.draw();
        const std::map<Key, std::pair<uint32_t, uint32_t>> liveInks = staticInks(livePl);
        float prevEnd = dataEnd(livePl);
        P.eq("dot.live", b(tagged(livePl, ui::tag::opDot).size() == 1), 1);
        png(P, host, dir, "stale-" + key + "-live");

        // ---- stale: 2 s without a frame ---------------------------------------------------------------------------------
        const L::HistoryGeom& hg = L::kBandHistory;
        const double pxPerMs = static_cast<double>(hg.colWidth) * static_cast<double>(hg.columns)
                             / (static_cast<double>(ctx.historySpanTenths) * 100.0);
        const double stepPx = static_cast<double>(kDt) * 1000.0 * pxPerMs;
        std::size_t matchedMin = liveInks.size();
        int inkDiffs = 0, stepsBad = 0, steps = 0, dashes = 0, staleTicks = 0;
        float firstStaleEnd = -1.0f;
        uint32_t alphaHeld = 0, alphaFading = 0;
        bool goneAt1s = false;
        for (int k = 1; k <= kStaleFrames; ++k)
        {
            host.tick(1, kDt);
            const funkgui::PrimList& pl = host.draw();

            std::size_t m = 0;
            for (const funkgui::Prim& p : pl.prims)
                if (!isLive(p))
                    if (const auto it = liveInks.find(keyOf(p)); it != liveInks.end())
                    {
                        ++m;
                        if (it->second != std::pair<uint32_t, uint32_t>{ p.c0, p.c1 })
                            ++inkDiffs;
                    }
            matchedMin = std::min(matchedMin, m);

            const float end = dataEnd(pl);
            if (!ctx.frame.fresh)
            {
                ++staleTicks;
                if (firstStaleEnd < 0.0f)
                    firstStaleEnd = end;                          // the first stale frame also closes the partial column
                else
                {
                    ++steps;
                    if (!(std::fabs(static_cast<double>(prevEnd - end) - stepPx) <= 0.15))
                    {
                        ++stepsBad;
                        std::printf("NOTE     ui.live_stale: stale frame %d moved the data end %.3f px (want %.3f)\n", k,
                                    static_cast<double>(prevEnd - end), stepPx);
                    }
                }
            }
            prevEnd = end;

            const std::string v = displayItem(host, false);
            dashes += (v == "Gain reduction, no signal" || bandMeterDash(host)) ? 1 : 0;

            const std::vector<const funkgui::Prim*> dots = tagged(pl, ui::tag::opDot);
            if (k == 15 && dots.size() == 1)
                alphaHeld = alphaOf(*dots[0]);
            if (k == 45 && dots.size() == 1)
                alphaFading = alphaOf(*dots[0]);
            if (k == 60)
                goneAt1s = dots.empty() && tagged(pl, ui::tag::grNeedle).empty() && tagged(pl, ui::tag::targetDot).empty()
                        && tagged(pl, ui::tag::opTrail).empty();

            if (k == 15 || k == 30 || k == 45 || k == 60 || k == 90 || k == 120)
                png(P, host, dir, "stale-" + key + "-" + ms4(static_cast<double>(k) * static_cast<double>(kDt)));
        }
        const funkgui::PrimList& endPl = host.draw();
        std::printf("NOTE     ui.live_stale: %zu static primitives in the live frame, >= %zu drawn again, %d ink "
                    "differences; %d stale frames, the data end moved %.2f px after the first (%d steps of %.3f px, %d "
                    "off)\n", liveInks.size(), matchedMin, inkDiffs, staleTicks,
                    static_cast<double>(firstStaleEnd - dataEnd(endPl)), steps, stepPx, stepsBad);

        P.eq("feed.stale", b(ctx.frame.hasFrame && !ctx.frame.fresh), 1);
        P.eq("ink.static_same", inkDiffs, 0);
        P.ge("ink.static_matched", liveInks.empty() ? 0.0 : static_cast<double>(matchedMin)
                                                            / static_cast<double>(liveInks.size()), 0.9);
        P.eq("history.advances", b(steps >= 60 && stepsBad == 0), 1);
        P.near("history.advances_px", static_cast<double>(firstStaleEnd - dataEnd(endPl)),
               static_cast<double>(steps) * stepPx, 2.0);
        {
            float gapTo = -1.0f;
            for (const funkgui::Prim* p : tagged(endPl, ui::tag::gap))
                if (p->y0 >= hg.plot.y && p->y1 <= hg.plot.bottom() + 1.0f)
                    gapTo = std::max(gapTo, p->x1);
            P.eq("history.gap", b(gapTo >= hg.plot.right() - 3.5f && dataEnd(endPl) > hg.plot.x), 1);
        }
        P.eq("meters.floor", b(tagged(endPl, ui::tag::meterIn).empty() && tagged(endPl, ui::tag::meterOut).empty()
                               && tagged(endPl, ui::tag::meterGr).empty() && tagged(endPl, ui::tag::meterHold).empty()), 1);
        P.eq("readout.zero", b(displayItem(host, false) == "Gain reduction, 0.0 dB"
                               && displayItem(host, true) == std::string("IN ") + kMinusInf + " \xC2\xB7 OUT " + kMinusInf), 1);
        P.eq("readout.never_dash", dashes, 0);
        {
            int bars = 0;
            for (const funkgui::Prim* p : tagged(endPl, ui::tag::displayValue))
                bars += isLive(*p) && kindOf(*p) == funkgui::PrimKind::rrect ? 1 : 0;
            P.eq("bar.zero", bars, 0);
        }
        P.eq("dot.held_fresh", static_cast<int64_t>(alphaHeld), 255);
        P.eq("dot.fading", b(alphaFading > 0 && alphaFading < 255), 1);
        P.eq("dot.gone", b(goneAt1s), 1);
        std::printf("NOTE     ui.live_stale: OP_DOT alpha %u at 0.25 s, %u at 0.75 s\n", alphaHeld, alphaFading);

        // ---- the frame rate --------------------------------------------------------------------------------------------
        P.eq("rate.moving", b(panel.wantsFullRate()), 1);
        host.tick(360, kDt);                                      // 6 s more: the data scrolls out of the 5 s span
        P.eq("rate.idle", b(!panel.wantsFullRate()), 1);
        host.move(300.0f, 75.0f);                                 // over the display row, between readout and cells
        host.tick(1, kDt);
        P.eq("rate.hover", b(panel.wantsFullRate()), 1);
        host.tick(static_cast<int>(std::lround(static_cast<double>(L::live::kActiveS + 0.2f) / static_cast<double>(kDt))),
                  kDt);
        P.eq("rate.hover_ends", b(!panel.wantsFullRate()), 1);

        // ---- the Characteristics screen at rest --------------------------------------------------------------------------
        const ui::ViewSpec* chars = ui::findView("chars.sidechain");
        if (chars == nullptr)
        {
            P.harnessError("ui.live_stale: no chars.sidechain view");
            return;
        }
        panel.setView(*chars, true);
        host.tick(1, kDt);
        std::vector<std::string> rows;
        const uint32_t rbase = ui::plotIdBase(ui::ViewIndex::charScreen, 3);
        for (const funkgui::A11yItem& it : host.accessibility())
            if (it.id >= rbase + 2 && it.id < rbase + 2 + 18 && it.visible)
                rows.push_back(it.value);
        bool dashOk = true;
        for (std::size_t i = 0; i < rows.size(); ++i)
            dashOk = dashOk && (rows[i] != kDash || (i == 4 && desc.stage2 == fcdsp::Stage2Kind::none)
                                || i == 6 || i == 7);         // ATK / REL EFF: "–" when the Mode publishes no tau
        const bool restOk = rows.size() >= 10 && rows[0] == kMinusInf && rows[2] == "0.0" && rows[3] == "0.0"
                         && rows[9] == "IDLE";
        if (!restOk || !dashOk)
            for (std::size_t i = 0; i < rows.size(); ++i)
                std::printf("NOTE     ui.live_stale: READOUTS row %zu '%s'\n", i + 1, rows[i].c_str());
        P.eq("chars.rest", b(restOk && dashOk), 1);
        png(P, host, dir, "stale-" + key + "-chars");
    }

    // ---- a stop that starts and ends while the plot is hidden (S13 H1a, UF1a follow-up) ---------------------------------

    struct GapRun
    {
        float x0 = 0.0f, x1 = 0.0f;
    };

    // The GAP runs drawn inside `plot` (its dotted floor line: one primitive per dot, merged while < 4 px apart).
    std::vector<GapRun> gapRuns(const funkgui::PrimList& pl, const funkgui::Rect& plot)
    {
        std::vector<GapRun> runs;
        for (const funkgui::Prim* p : tagged(pl, ui::tag::gap))
        {
            const float cx = 0.5f * (p->x0 + p->x1), cy = 0.5f * (p->y0 + p->y1);
            if (!plot.contains({ cx, cy }))
                continue;
            if (!runs.empty() && p->x0 - runs.back().x1 < 4.0f)
                runs.back().x1 = std::max(runs.back().x1, p->x1);
            else
                runs.push_back({ p->x0, p->x1 });
        }
        return runs;
    }

    // HISTORY on the band (hidden on CHARACTERISTICS) and CONTROL PATH (hidden on PANEL): 1 s of audio, then the other
    // screen is shown for 1.5 s of silence (the host stopped) and 1 s of audio again, then the plot's screen comes
    // back. Its gaps must be exactly those of the same plot left in view all along.
    void hiddenGap(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const ui::ViewSpec* panelView = ui::findView("panel");
        const ui::ViewSpec* charsView = ui::findView("chars.sidechain");
        if (panelView == nullptr || charsView == nullptr)
        {
            P.harnessError("ui.live_stale: no panel or chars.sidechain view");
            return;
        }
        for (const bool band : { true, false })
        {
            const ui::ViewSpec& home = band ? *panelView : *charsView;
            const ui::ViewSpec& away = band ? *charsView : *panelView;
            const funkgui::Rect plot = band ? L::kBandHistory.plot : L::kControlPath.plot;
            std::array<std::vector<GapRun>, 2> runs;               // [0]: in view all along, [1]: hidden in between
            for (int hide = 0; hide < 2; ++hide)
            {
                FakeFacade facade(entry.desc->key);
                ui::Panel panel(facade, kOpts);
                funkgui::HeadlessHost host(panel, 0, 2.0f);
                panel.setView(home, true);
                host.settle(600, kDt);
                const auto slot0 = static_cast<uint8_t>(panel.context().frame.res.view.slot);
                Script s{ facade, FakeFacade::quietFrame(slot0, panel.context().frame.res.eng), slot0, 0.0, {}, 0.0f,
                          0.0f };
                int k = 0;
                for (; k < 60; ++k)
                {
                    s.frame(k);
                    host.tick(1, kDt);
                }
                if (hide == 1)
                    panel.setView(away, true);
                host.tick(90, kDt);                                // 1.5 s without a frame
                for (int i = 0; i < 60; ++i, ++k)
                {
                    s.frame(k);
                    host.tick(1, kDt);
                }
                if (hide == 1)
                    panel.setView(home, true);
                host.tick(1, kDt);
                runs[static_cast<std::size_t>(hide)] = gapRuns(host.draw(), plot);
            }
            bool same = runs[0].size() == runs[1].size();
            for (std::size_t i = 0; same && i < runs[0].size(); ++i)
                same = std::fabs(runs[0][i].x0 - runs[1][i].x0) <= 1.0f
                    && std::fabs(runs[0][i].x1 - runs[1][i].x1) <= 1.0f;
            const std::string k = std::string("hidden_gap.") + (band ? "history" : "control_path");
            std::printf("NOTE     %s: %zu run(s) in view all along, %zu hidden in between\n", k.c_str(), runs[0].size(),
                        runs[1].size());
            for (std::size_t h = 0; h < 2; ++h)
                for (const GapRun& g : runs[h])
                    std::printf("NOTE       %s x %.1f–%.1f\n", h == 0 ? "shown " : "hidden", static_cast<double>(g.x0),
                                static_cast<double>(g.x1));
            P.ge(k + ".reference", static_cast<double>(runs[0].size()), 1.0);
            P.eq(k + ".same", b(same), 1);
        }
    }
}

FCMP_PROBE(ui, live_stale)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.live_stale reads UiPreferences: set FCMP_PREFS_DIR to a sandbox (CTest does)");
        return P.finish();
    }
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.live_stale: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    funkgui::UiPreferences::get().setInt("historySpanTenths", L::kDefaultSpanTenths);
    funkgui::UiPreferences::get().setInt("meterScaleDb", L::kDefaultScaleDb);
    run(P, *entry);
    hiddenGap(P, *entry);
    return P.finish();
}
