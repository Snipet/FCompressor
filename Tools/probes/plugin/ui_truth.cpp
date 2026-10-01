// FCMP_PROBE layer=ui name=truth scope=mode timeout=300
//
// ui.truth.<key> (03 §3.6, C's G3; U2 band rows): what the band draws equals what the engine did. The Panel runs over
// EngineFacade (a real fcdsp::EngineHost at 48 kHz, blocks of 128, its own UiFrame and HistoryRing, a TestTap on every
// sample; no JUCE processor, SPRINTS D11), ticked at 60 Hz while the engine renders 800 samples per frame of a
// deterministic program: a quiet lead-in, eight decaying 1 kHz bursts around the Mode's threshold, a quiet gap, then
// 1.5 s of a steady 1 kHz tone at x_L = min(T_in + 12, −3) dB on the Mode's detector axis (rms: the sine peak is
// +3.01 dB). The last frame is drawn and read back through its primitives and axis records (LEVEL, HIST_AXIS).
//
// Band rows (spec; 1 px = 0.25 dB at the default 48 dB scale):
//   truth.frame.live            the feed is live and the curves carry the smoothed fields (FrameState::overlaid)
//   truth.opdot.drawn_px        OP_DOT's centre at (x(cx), y(cx − appliedGrDb)), GR of the lane with the larger GR and
//                               cx the peak envelope TransferPlot defines (max of the store's detMaxDb over the last
//                               10 ms and the frame's curveXDb), <= 0.5 px: the plot draws the telemetry
//   truth.opdot.tap_px          that operating point equals the tap's: max(detDb − preGainDb) over the same samples and
//                               the GR of the last one, <= 0.5 px
//   truth.opdot.d1_px           <= 1 px from the D1-measured operating point: x_L for a final Mode with a peak or rms
//                               law (its detector is held to the declared axis), else the tap's detector envelope over
//                               D1's window (custom and true-peak laws; provisional Modes, whose fidelity rows are NOTEs);
//                               and the tap's mean applied GR over the last 100 ms (D1's window). The audio-derived GR (single-bin DFT in → out, makeup and
//                               pre-gain removed) is printed as a NOTE: colour stages move it, not the drawing.
//   truth.target.px             TARGET_DOT at (x(cx), y(cx − the envelope of tgtMaxDb / targetGrDb)) <= 0.5 px
//   truth.meter_gr.px           METER_GR's length equals the processor's GR (the tap's last sample, lane max) · px/dB
//                               <= 0.5 px
//   truth.needle.px             GR_NEEDLE's length equals METER_GR's <= 0.5 px (one GR, three drawings)
//   truth.meter_in.px           METER_IN (left) at y(inPeakDb[0]) <= 0.5 px
//   truth.history.newest_db     the store's newest column's grMaxDb equals the tap's max GR over its 48 samples <= 0.1 dB
//   truth.history.drawn_gr_db   HIST_GR's newest drawn value equals the tap's max GR over that plot column <= 0.1 dB
//   truth.history.drawn_in_db   HIST_IN's newest drawn value equals the input's peak over that plot column <= 0.1 dB
//   truth.history.laps          0 gap markers (the store drains every frame); truth.history.attach_gap: the first
//                               column after the attach carries b5
//   truth.curve.overlay_px      TRANSFER_CURVE lies on staticGain over resolve(raw) + overlaySmoothed(frame) <= 0.5 px
//                               (K1 #7, K2 #24: never from UiFrame alone)
//   truth.threshold.px          THRESHOLD_MARK at y(T_in) of that EngineParams <= 0.5 px
//   truth.slot.det              (UF1a) the THRESHOLD slot's "DET" readout prints the operating dot's x (the 10 ms peak
//                               envelope), 1 decimal, when the slot shows one
// GAIN REDUCTION rows (UF1a, ADR-70), checked on every frame of the run:
//   truth.display.hold_db       at each refresh (every 0.25 s of panel time, and the first frame), the readout equals
//                               the tap's max GR (lane max) over the last 1 s of audio: the samples of the store's newest
//                               1000 columns and the newest block, <= 0.1 dB (worst over the run)
//   truth.display.refreshes     the readout refreshed >= 4 times per second of the run (>= 15 in the 4.3 s program)
//   truth.display.quantised     its text never changed between two refreshes (quantised in time, not per frame)
// Characteristics rows (U3, S9; 03 §3.6 "the CONTROL PATH internal0 lane's newest point, and READOUTS rows 11–18, equal
// UiFrame.internals[i] through their declared ranges"): at the end of the same run the Panel switches to chars.sidechain
// (instant) and draws one more frame; no audio is rendered in between, so the feed is still fresh and the frame the same.
//   truth.chars.live            the Characteristics screen is shown and the feed still live
//   truth.cp.applied_db         CONTROL PATH's newest column (CP_APPLIED area, through CP_AXIS) equals the tap's max GR
//                               over that plot column's samples (the louder lane) <= 0.1 dB
//   truth.cp.min_db             its grMinDb line equals the tap's min over those samples of the louder lane's GR <= 0.1 dB
//   truth.cp.target_db          CP_TARGET's newest column equals the tap's max target GR over those samples <= 0.1 dB
//   truth.cp.hist_aligned       CONTROL PATH's column edges are HISTORY's (HIST_GR's segment edges)
//   truth.cp.internal_px        the internal lane's newest point equals UiFrame.internals[h] (h: the history-flagged
//                               internal) through its declared lo…hi, <= 0.5 px of the lane
//   truth.readouts.internal.<i> READOUTS row 11+i prints UiFrame.internals[i] with its declared decimals and unit (a11y
//                               row = the text drawn), for every declared internal
//   truth.readouts.{det,target,applied}  rows 1, 3, 4: the operating dot's x (the 10 ms envelope), the target dot's GR
//                               and the frame's applied GR, of the lane with the larger GR, 1 decimal
// History store rows (a FakeFacade with scripted frames and columns; 01 §6.3, 02 §6.5, §9.6):
//   history.lap.*               a lap (5000 columns between two drains) leaves exactly one gap marker, drawn as GAP, and
//                               every trace breaks there (never interpolated)
//   history.mode_tick.*         a change of the columns' Mode slot draws one MODE_TICK where it happened (±1 px)
//   history.stale_scrolls_px    (UF1a, ADR-69) with the feed stale the strip keeps scrolling at wall-clock rate: the
//                               newest data column moves left by one frame's time × px/ms, ±0.15 px
//   history.stale_gap           the time since the audio stopped is a GAP run reaching the plot's right edge
//   history.resume.lands_now    the audio's return lands at "now" (the right edge) after the gap
//   history.freeze.*            press and hold sets PanelContext::freeze at the column under the pointer; release ends it
//   history.state_lane          the state lane draws 1..40 merged runs
//   history.span.click          a click on the 10 S cell sets the preference and the span (UiPreferences, sandboxed)
//
// Visual inspection (not a test): probe-own flags after "--":  -- --png <end.png> [--png-mid <mid.png>] [--dump <x>]
// [--png-chars <chars.png>] [--png-seq <dir>] write the last frame (and the frame 2.0 s in, mid-bursts; the
// Characteristics screen at the end; and, with --png-seq, the panel at every GAIN REDUCTION refresh as
// <dir>/gr-<ms>.png: the readout at 0.25 s intervals under the moving GR) at dpi 2.
//
// The probe writes UiPreferences (the span click, and "grView" = HISTORY so the band draws the traces it reads, UF2): it
// refuses to run without FCMP_PREFS_DIR (CTest sets a sandbox).
#include "ProbeRegistry.h"

#include "EngineFacade.h"
#include "FakeFacade.h"
#include "Measure.h"

#include "editor/HistoryStore.h"
#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Axis.h>
#include <funkgui/canvas/Prim.h>
#include <funkgui/canvas/PrimList.h>
#include <funkgui/core/Format.h>
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
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    using funkgui::test::Probe;
    namespace ui = fcmp::ui;
    namespace layout = fcmp::ui::layout;
    namespace probe = fcmp::probe;

    constexpr float kDt = 1.0f / 60.0f;
    constexpr uint64_t kSamplesPerFrame = 800;                   // 48 kHz / 60 Hz
    constexpr ui::PanelOptions kOpts { true, true, false };       // skipHint, syncPreview, live
    constexpr double kD1WindowS = 0.1;                            // D1: 0.1 s of 1 kHz, whole cycles
    constexpr float kHz = 1000.0f;

    // ---- the frame, read back -------------------------------------------------------------------------------------------

    struct Box { float x = 0, y = 0, w = 0, h = 0; };
    struct Seg { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
    struct Col { float x0 = 0, x1 = 0, top0 = 0, top1 = 0, bot0 = 0, bot1 = 0; };

    Box boxOf(const funkgui::Prim& p)                             // an rrect's own rectangle (apron removed)
    {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        return { cx - p.d0[2], cy - p.d0[3], 2.0f * p.d0[2], 2.0f * p.d0[3] };
    }

    Seg segOf(const funkgui::Prim& p)
    {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        return { cx + p.d1[0], cy + p.d1[1], cx + p.d1[2], cy + p.d1[3] };
    }

    Col colOf(const funkgui::Prim& p)
    {
        const float cy = 0.5f * (p.y0 + p.y1);
        return { p.x0, p.x1, cy + p.d1[0], cy + p.d1[1], cy + p.d1[2], cy + p.d1[3] };
    }

    std::vector<const funkgui::Prim*> tagged(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        std::vector<const funkgui::Prim*> v;
        for (const funkgui::Prim& p : pl.prims)
            if (p.tag == t)
                v.push_back(&p);
        return v;
    }

    const funkgui::AxisRec* axisOf(const funkgui::PrimList& pl, funkgui::Tag t)
    {
        for (const funkgui::AxisRec& a : pl.axes)
            if (a.tag == t)
                return &a;
        return nullptr;
    }

    float toPx(const funkgui::AxisMap& m, float v) { return m.px0 + (v - m.v0) * (m.px1 - m.px0) / (m.v1 - m.v0); }
    float toValue(const funkgui::AxisMap& m, float px) { return m.v0 + (px - m.px0) * (m.v1 - m.v0) / (m.px1 - m.px0); }

    float maxLane(float a, float b) { return std::max(a, b); }

    // ---- probe-own flags ----------------------------------------------------------------------------------------------------

    struct Flags
    {
        std::string png, pngMid, dump, pngChars, pngSeq;
    };

    Flags flags()
    {
        Flags f;
        const int argc = fcmp::probe::argc();
        char** argv = fcmp::probe::argv();
        bool own = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a = argv[i] != nullptr ? argv[i] : "";
            if (a == "--")
            {
                own = true;
                continue;
            }
            if (!own || i + 1 >= argc || argv[i + 1] == nullptr)
                continue;
            if (a == "--png")
                f.png = argv[++i];
            else if (a == "--png-mid")
                f.pngMid = argv[++i];
            else if (a == "--dump")
                f.dump = argv[++i];
            else if (a == "--png-chars")
                f.pngChars = argv[++i];
            else if (a == "--png-seq")
                f.pngSeq = argv[++i];
        }
        return f;
    }

    bool makeParent(const std::string& path)
    {
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        std::error_code ec;
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);
        return !ec;
    }

    void writePng(Probe& P, funkgui::HeadlessHost& host, const std::string& path)
    {
        if (path.empty())
            return;
        host.draw();
        if (!makeParent(path) || !host.writePng(path.c_str(), 2))
            P.harnessError("ui.truth: cannot write " + path);
        else
            std::printf("NOTE     ui.truth: %s\n", path.c_str());
    }

    // ---- the GAIN REDUCTION readout (ADR-70) ------------------------------------------------------------------------------

    // The display row's spoken value: "Gain reduction, −4.2 dB" -> 4.2, "Gain reduction, 0.0 dB" -> 0; NaN otherwise
    // (no signal, or a slot shown).
    float displayGr(funkgui::HeadlessHost& host)
    {
        for (const funkgui::A11yItem& it : host.accessibility())
            if (ui::viewIndexOf(it.id) == static_cast<int>(ui::ViewIndex::displayRow) && it.title == "Display")
            {
                const std::string_view v = it.value;
                constexpr std::string_view head = "Gain reduction, ";
                if (v.substr(0, head.size()) != head)
                    return std::nanf("");
                std::string num(v.substr(head.size()));
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
        return std::nanf("");
    }

    // ---- band rows over the real engine ---------------------------------------------------------------------------------

    void engineRows(Probe& P, const fcdsp::ModeEntry& entry, const Flags& fl)
    {
        const fcdsp::ModeDescriptor& desc = *entry.desc;

        // The operating level: 12 dB over T_in on the Mode's detector axis, at most −3 dB.
        fcdsp::RawParams raw0;
        fcdsp::Resolution res0;
        {
            probe::FakeFacade tmp(desc.key);
            raw0 = tmp.currentRaw();
            fcdsp::resolve(entry, raw0, res0);
        }
        const fcdsp::DetectorLaw law = desc.detectorLaw != nullptr ? desc.detectorLaw(res0.eng) : fcdsp::DetectorLaw::peak;
        const float tIn = fcdsp::analysis::inputThresholdDb(res0.eng);
        const float xL = std::min(tIn + 12.0f, -3.0f);
        const float peakL = xL + (law == fcdsp::DetectorLaw::rms ? 3.0103f : 0.0f);
        const std::array<float, 8> bursts { 8.0f, 0.0f, 4.0f, -4.0f, 10.0f, 2.0f, 6.0f, -2.0f };
        std::vector<probe::Program::Tone> tones;
        tones.push_back({ 0.3, peakL - 30.0f, kHz, 0.0f });
        for (const float b : bursts)
            tones.push_back({ 0.25, std::min(peakL + b, -0.5f), kHz, 30.0f });
        tones.push_back({ 0.5, peakL - 24.0f, kHz, 0.0f });
        tones.push_back({ 1.5, peakL, kHz, 0.0f });
        const probe::Program program(std::move(tones));
        const uint64_t total = program.length();
        std::printf("NOTE     ui.truth: T_in %.2f dB, x_L %.2f dB (%s law), steady peak %.2f dBFS, %llu samples\n",
                    static_cast<double>(tIn), static_cast<double>(xL),
                    law == fcdsp::DetectorLaw::rms ? "rms" : law == fcdsp::DetectorLaw::peak ? "peak" : "custom",
                    static_cast<double>(peakL), static_cast<unsigned long long>(total));

        probe::EngineFacade facade(desc.key, total + 1024);
        ui::Panel panel(facade, kOpts);
        facade.setUiAttached(true);                               // the editor's lifetime gate (01 §6.3)
        funkgui::HeadlessHost host(panel, 0, 2.0f);
        host.settle(600, kDt);

        const uint64_t mid = static_cast<uint64_t>(2.0 * probe::EngineFacade::kFs);
        bool midShot = false;
        // ADR-70: the GAIN REDUCTION readout at every refresh against the tap, and never changing between two.
        const ui::PanelContext& pc = panel.context();
        const auto refreshSlot = [&]() {
            return static_cast<int64_t>(std::floor(pc.seconds / static_cast<double>(layout::display::kGrRefreshS)));
        };
        int64_t lastSlot = refreshSlot();
        std::string lastText;
        bool seen = false;
        double worstHold = 0.0;
        int refreshes = 0, between = 0;
        for (uint64_t frame = 1; facade.processed() + probe::EngineFacade::kBlock <= total; ++frame)
        {
            const uint64_t target = std::min<uint64_t>(frame * kSamplesPerFrame, total);
            if (target > facade.processed())
            {
                const auto blocks = static_cast<int>((target - facade.processed()) / probe::EngineFacade::kBlock);
                facade.render(program, std::max(blocks, 1));
            }
            host.tick(1, kDt);
            if (!midShot && facade.processed() >= mid)
            {
                midShot = true;
                writePng(P, host, fl.pngMid);
            }

            const float shown = displayGr(host);
            const int64_t slot = refreshSlot();
            char text[32];
            std::snprintf(text, sizeof text, "%.1f", static_cast<double>(shown));
            if (!std::isnan(shown) && (slot != lastSlot || !seen))
            {
                // The reference: the tap's max GR over the store's newest 1000 columns (48 samples each) and the newest
                // block (up to the last sample), the frame being fresh on every tick of this run.
                const uint64_t lastS = facade.processed() - 1;
                const uint64_t cols = pc.history.count();
                const uint64_t w = std::min<uint64_t>(cols, 1000);
                float ref = 0.0f;
                for (uint64_t n = (cols - w) * 48; n <= lastS; ++n)
                    ref = std::max(ref, maxLane(facade.tapGr(n, 0), facade.tapGr(n, 1)));
                const float want = ref > layout::display::kGrShownDb ? ref : 0.0f;
                worstHold = std::max(worstHold, static_cast<double>(std::fabs(shown - want)));
                ++refreshes;
                if (!fl.pngSeq.empty())
                    writePng(P, host, fl.pngSeq + "/gr-" + std::to_string(std::lround(pc.seconds * 1000.0)) + ".png");
            }
            else if (seen && text != lastText)
            {
                ++between;
            }
            if (!std::isnan(shown))
            {
                seen = true;
                lastText = text;
            }
            lastSlot = slot;
        }
        const double runS = static_cast<double>(total) / probe::EngineFacade::kFs;
        std::printf("NOTE     ui.truth: GAIN REDUCTION: %d refreshes over %.2f s, worst %.3f dB from the tap's 1 s max, "
                    "%d changes between refreshes\n", refreshes, runS, worstHold, between);
        P.le("truth.display.hold_db", refreshes > 0 ? worstHold : 99.0, 0.1);
        P.ge("truth.display.refreshes", refreshes, std::floor(runS * 4.0) - 2.0);
        P.eq("truth.display.quantised", between, 0);
        const uint64_t last = facade.processed() - 1;             // the sample the last UiFrame ended on

        const ui::PanelContext& ctx = panel.context();
        const ui::FrameState& f = ctx.frame;
        const funkgui::PrimList& pl = host.draw();
        P.eq("truth.frame.live", f.live && f.overlaid ? 1 : 0, 1);

        const layout::TransferGeom& tg = layout::kBandTransfer;
        const funkgui::AxisRec* level = axisOf(pl, ui::tag::level);
        if (level == nullptr || !level->hasX || !level->hasY)
        {
            P.harnessError("ui.truth: no LEVEL axis in the panel frame");
            return;
        }
        const float scale = static_cast<float>(ctx.meterScaleDb);
        const float ppd = tg.level.pxPerDb(scale);
        const auto ax = [&](float db) { return toPx(level->x, db); };
        const auto ay = [&](float db) { return toPx(level->y, db); };

        // The drawn operating point (TransferPlot's rule), the tap's, and D1's.
        const ui::HistoryStore& h = ctx.history;
        const int lane = f.ui.appliedGrDb[1] > f.ui.appliedGrDb[0] ? 1 : 0;
        const auto ul = static_cast<std::size_t>(lane);
        const uint64_t count = h.count();
        const uint64_t e0 = count >= 10 ? count - 10 : 0;         // the last 10 ms of the store
        float cx = f.ui.curveXDb[ul], tgt = f.ui.targetGrDb[ul];
        for (uint64_t e = std::max(e0, h.oldest()); e < count; ++e)
            if (!ui::HistoryStore::isGap(h.at(e)))
            {
                cx = std::max(cx, h.at(e).detMaxDb);
                tgt = std::max(tgt, h.at(e).tgtMaxDb);
            }
        const float gr = f.ui.appliedGrDb[ul];
        const float pre = f.eng.preGainDb;
        float tapX = -200.0f;
        for (uint64_t n = e0 * 48; n <= last; ++n)
            tapX = std::max(tapX, maxLane(facade.tapDet(n, 0), facade.tapDet(n, 1)) - pre);
        const float tapGr = maxLane(facade.tapGr(last, 0), facade.tapGr(last, 1));
        const auto window = static_cast<uint64_t>(kD1WindowS * probe::EngineFacade::kFs);
        double meanGr = 0.0;
        float envX = -200.0f;                                     // the tap's detector envelope over D1's window
        for (uint64_t n = last + 1 - window; n <= last; ++n)
        {
            meanGr += static_cast<double>(maxLane(facade.tapGr(n, 0), facade.tapGr(n, 1)));
            envX = std::max(envX, maxLane(facade.tapDet(n, 0), facade.tapDet(n, 1)) - pre);
        }
        meanGr /= static_cast<double>(window);
        // D1's x: the stimulus on the declared axis where the Mode's detector is held to it (a final Mode with a peak
        // or rms law, whose dsp.static fidelity rows are blocking); else (custom and true-peak laws, provisional Modes
        // whose fidelity rows are NOTEs, SPRINTS §7 D12) the level the detector measured.
        const bool calibrated = !desc.provisional && (law == fcdsp::DetectorLaw::peak || law == fcdsp::DetectorLaw::rms);
        const float d1x = calibrated ? xL : envX;
        const float d1y = d1x - static_cast<float>(meanGr);

        {   // the audio-derived GR, for the record (D1's single-bin DFT over the same window)
            const probe::measure::SingleBin bin(kHz, probe::EngineFacade::kFs, static_cast<std::size_t>(window));
            const auto n0 = static_cast<std::int64_t>(last + 1 - window);
            const double g = bin.gainDb(facade.input(0).subspan(static_cast<std::size_t>(n0), window),
                                        facade.output(0).subspan(static_cast<std::size_t>(n0), window), n0);
            const double audioGr = static_cast<double>(pre + f.eng.makeupDb) - g;
            std::printf("NOTE     ui.truth: steady x %.3f dB (detector envelope %.3f, declared-axis x_L %.3f%s), GR tap "
                        "mean %.3f dB, audio-derived %.3f dB (latency %d samples)\n", static_cast<double>(cx),
                        static_cast<double>(envX), static_cast<double>(xL), calibrated ? ", the D1 reference" : "",
                        meanGr, audioGr, facade.latencySamples());
        }

        const std::vector<const funkgui::Prim*> dots = tagged(pl, ui::tag::opDot);
        P.eq("truth.opdot.found", dots.size() == 1 ? 1 : 0, 1);
        if (dots.size() == 1)
        {
            const Box b = boxOf(*dots[0]);
            const float dx = b.x + 0.5f * b.w, dy = b.y + 0.5f * b.h;
            P.le("truth.opdot.drawn_px", std::max(std::fabs(dx - ax(cx)), std::fabs(dy - ay(cx - gr))), 0.5);
            P.le("truth.opdot.tap_px", std::max(std::fabs(ax(tapX) - ax(cx)), std::fabs(ay(tapX - tapGr) - ay(cx - gr))),
                 0.5);
            P.le("truth.opdot.d1_px", std::max(std::fabs(dx - ax(d1x)), std::fabs(dy - ay(d1y))), 1.0);
        }
        const std::vector<const funkgui::Prim*> rings = tagged(pl, ui::tag::targetDot);
        if (rings.size() == 1)
        {
            const Box b = boxOf(*rings[0]);
            P.le("truth.target.px", std::max(std::fabs(b.x + 0.5f * b.w - ax(cx)), std::fabs(b.y + 0.5f * b.h - ay(cx - tgt))),
                 0.5);
        }
        else
        {
            P.eq("truth.target.found", static_cast<int64_t>(rings.size()), 1);
        }

        // METER_GR, GR_NEEDLE, METER_IN.
        const std::vector<const funkgui::Prim*> grBars = tagged(pl, ui::tag::meterGr);
        const std::vector<const funkgui::Prim*> needles = tagged(pl, ui::tag::grNeedle);
        float barLen = -1.0f;
        if (grBars.size() == 1)
        {
            barLen = boxOf(*grBars[0]).h;
            P.le("truth.meter_gr.px", std::fabs(barLen - tapGr * ppd), 0.5);
        }
        else
        {
            P.eq("truth.meter_gr.found", static_cast<int64_t>(grBars.size()), 1);
        }
        if (needles.size() == 1 && barLen >= 0.0f)
            P.le("truth.needle.px", std::fabs(boxOf(*needles[0]).h - barLen), 0.5);
        else
            P.eq("truth.needle.found", static_cast<int64_t>(needles.size()), 1);
        {
            const layout::MeterGeom::Bar& inL = layout::kBandMeters.bars[0];
            float top = -1.0f;
            for (const funkgui::Prim* p : tagged(pl, ui::tag::meterIn))
            {
                const Box b = boxOf(*p);
                if (std::fabs(b.x - inL.r.x) < 0.01f && std::fabs(b.w - inL.r.w) < 0.01f)
                    top = top < 0.0f ? b.y : std::min(top, b.y);
            }
            P.le("truth.meter_in.px", top < 0.0f ? 99.0 : std::fabs(top - tg.level.y(f.ui.inPeakDb[0], scale)), 0.5);
        }

        // History: the store's newest column and the newest drawn plot column against the tap.
        P.eq("truth.history.laps", h.gaps(), 0);
        P.eq("truth.history.attach_gap", h.count() > 0 && ui::HistoryStore::isGap(h.at(0)) ? 1 : 0, 1);
        const auto tapMaxGr = [&](uint64_t a, uint64_t b) {       // entries [a, b): 48 samples each
            float m = 0.0f;
            for (uint64_t n = a * 48; n < b * 48 && n <= last; ++n)
                m = std::max(m, maxLane(facade.tapGr(n, 0), facade.tapGr(n, 1)));
            return m;
        };
        const auto inputPeak = [&](uint64_t a, uint64_t b) {
            float m = 0.0f;
            for (uint64_t n = a * 48; n < b * 48 && n <= last; ++n)
                m = std::max(m, std::max(std::fabs(facade.input(0)[n]), std::fabs(facade.input(1)[n])));
            return static_cast<float>(probe::measure::dbFromAmplitude(static_cast<double>(m)));
        };
        if (count > 0)
            P.le("truth.history.newest_db", std::fabs(h.at(count - 1).grMaxDb - tapMaxGr(count - 1, count)), 0.1);
        {
            // The newest plot column (HistoryPlot's rule): W = span / columns ms, the partial one [ceil(K W), count) when
            // it holds an entry, else the last complete one.
            const layout::HistoryGeom& hg = layout::kBandHistory;
            const double w = static_cast<double>(ctx.historySpanTenths) * 100.0 / static_cast<double>(hg.columns);
            const double k = std::floor(static_cast<double>(count) / w);
            auto c0 = static_cast<uint64_t>(std::ceil(k * w));
            uint64_t c1 = count;
            if (c0 >= c1)
            {
                c1 = c0;
                c0 = static_cast<uint64_t>(std::ceil((k - 1.0) * w));
            }
            float grDrawn = -1.0f, inDrawn = -1.0f;
            for (const funkgui::Prim* p : tagged(pl, ui::tag::histGr))
                if (const Col c = colOf(*p); std::fabs(c.x1 - hg.plot.right()) < 0.01f)
                    grDrawn = (c.bot1 - hg.plot.y) / ppd;
            for (const funkgui::Prim* p : tagged(pl, ui::tag::histIn))
                if (const Col c = colOf(*p); std::fabs(c.x1 - hg.plot.right()) < 0.01f)
                    inDrawn = hg.level.db(c.top1, scale);
            P.le("truth.history.drawn_gr_db", grDrawn < 0.0f ? 99.0 : std::fabs(grDrawn - tapMaxGr(c0, c1)), 0.1);
            P.le("truth.history.drawn_in_db", inDrawn < -900.0f ? 99.0 : std::fabs(inDrawn - inputPeak(c0, c1)), 0.1);
        }

        // THRESHOLD's DET readout (UF1a; the U1s follow-up): the operating dot's x, the 10 ms peak envelope, so it does not
        // jitter with the waveform's phase — never the raw curveXDb.
        {
            funkgui::ValueView v;
            ctx.slot(fcdsp::Pid::thr).view(v);
            const long long t = std::llround(static_cast<double>(cx) * 10.0);
            const long long a = t < 0 ? -t : t;
            char want[40];
            std::snprintf(want, sizeof want, "DET %s%lld.%lld", t < 0 ? "\xE2\x88\x92" : "", a / 10, a % 10);
            const std::string got = v.text.sub;
            if (got.rfind("DET ", 0) == 0)
                P.eq("truth.slot.det", got == want ? 1 : 0, 1);
            else
                std::printf("NOTE     ui.truth: THRESHOLD shows no DET readout ('%s')\n", got.c_str());
            if (got.rfind("DET ", 0) == 0 && got != want)
                std::printf("NOTE     ui.truth: THRESHOLD sub '%s', want '%s' (raw curveXDb %.2f)\n", got.c_str(), want,
                            static_cast<double>(f.ui.curveXDb[ul]));
        }

        // The curve and the threshold line from resolve(raw) + overlaySmoothed(frame) (K1 #7, K2 #24).
        {
            fcdsp::Resolution res;
            fcdsp::resolve(entry, facade.currentRaw(), res);
            fcdsp::EngineParams eng = res.eng;
            fcdsp::overlaySmoothed(f.ui, eng);
            double worst = 0.0;
            int n = 0;
            for (const funkgui::Prim* p : tagged(pl, ui::tag::transferCurve))
            {
                const Seg s = segOf(*p);
                for (const auto& [px, py] : { std::pair{ s.x0, s.y0 }, std::pair{ s.x1, s.y1 } })
                {
                    const float xd = toValue(level->x, px);
                    float g = 0.0f;
                    fcdsp::analysis::staticGain(entry, eng, std::span<const float>(&xd, 1), std::span<float>(&g, 1));
                    const float yWant = ay(xd + g - eng.preGainDb);
                    if (yWant < tg.plot.y - 0.5f || yWant > tg.plot.bottom() + 0.5f)
                        continue;                                 // a clipped end: the chord meets the frame
                    worst = std::max(worst, static_cast<double>(std::fabs(py - yWant)));
                    ++n;
                }
            }
            P.le("truth.curve.overlay_px", n > 0 ? worst : 99.0, 0.5);
            const std::vector<const funkgui::Prim*> marks = tagged(pl, ui::tag::thresholdMark);
            const float t = fcdsp::analysis::inputThresholdDb(eng);
            if (marks.size() == 1)
            {
                const Box b = boxOf(*marks[0]);
                P.le("truth.threshold.px", std::fabs(b.y + 0.5f * b.h - ay(t)), 0.5);
            }
            else
            {
                P.eq("truth.threshold.found", static_cast<int64_t>(marks.size()), 1);
            }
        }

        writePng(P, host, fl.png);
        if (!fl.dump.empty() && (!makeParent(fl.dump) || !host.writeDump(fl.dump.c_str())))
            P.harnessError("ui.truth: cannot write " + fl.dump);

        // ---- the Characteristics screen over the same run (U3, S9): CONTROL PATH and READOUTS ------------------------
        const ui::ViewSpec* chars = ui::findView("chars.sidechain");
        if (chars == nullptr)
        {
            P.harnessError("ui.truth: no chars.sidechain view");
            return;
        }
        panel.setView(*chars, true);
        host.tick(1, kDt);                                        // no audio in between: the same frame, still fresh
        const funkgui::PrimList& cl = host.draw();
        P.eq("truth.chars.live", panel.screen() == ui::Screen::characteristics && f.live ? 1 : 0, 1);
        const funkgui::AxisRec* cpAx = axisOf(cl, ui::tag::cpAxis);
        if (cpAx == nullptr || !cpAx->hasY)
        {
            P.harnessError("ui.truth: no CP_AXIS on the Characteristics screen");
            return;
        }
        {
            // The newest plot column (HISTORY's rule, W = span / 210 ms on this screen): the partial one when it holds an
            // entry, else the last complete one; its value is the strip's last sample, at the plot's right edge.
            const layout::ControlPathGeom& cg = layout::kControlPath;
            const double w = static_cast<double>(ctx.historySpanTenths) * 100.0 / static_cast<double>(cg.columns);
            const double k = std::floor(static_cast<double>(count) / w);
            auto c0 = static_cast<uint64_t>(std::ceil(k * w));
            uint64_t c1 = count;
            if (c0 >= c1)
            {
                c1 = c0;
                c0 = static_cast<uint64_t>(std::ceil((k - 1.0) * w));
            }
            float grMax = 0.0f, grMin = 1.0e9f, tgtMax = 0.0f;
            for (uint64_t n = c0 * 48; n < c1 * 48 && n <= last; ++n)
            {
                const float g = maxLane(facade.tapGr(n, 0), facade.tapGr(n, 1));
                grMax = std::max(grMax, g);
                grMin = std::min(grMin, g);
                tgtMax = std::max(tgtMax, maxLane(facade.tapTgt(n, 0), facade.tapTgt(n, 1)));
            }
            const auto newest = [&](funkgui::Tag t, bool clearFill) {
                float v = -1.0e9f;
                for (const funkgui::Prim* p : tagged(cl, t))
                    if (const Col c = colOf(*p); std::fabs(c.x1 - cg.plot.right()) < 0.01f && ((p->c0 >> 24) == 0) == clearFill)
                        v = toValue(cpAx->y, c.bot1);
                return v;
            };
            const float app = newest(ui::tag::cpApplied, false), mn = newest(ui::tag::cpApplied, true);
            const float tg0 = newest(ui::tag::cpTarget, true);
            P.le("truth.cp.applied_db", app < -1.0e8f ? 99.0 : std::fabs(app - grMax), 0.1);
            P.le("truth.cp.min_db", mn < -1.0e8f ? 99.0 : std::fabs(mn - grMin), 0.1);
            P.le("truth.cp.target_db", tg0 < -1.0e8f ? 99.0 : std::fabs(tg0 - tgtMax), 0.1);
            std::printf("NOTE     ui.truth: CONTROL PATH newest column [%llu, %llu) ms: applied %.3f (tap %.3f), min %.3f "
                        "(tap %.3f), target %.3f (tap %.3f) dB\n", static_cast<unsigned long long>(c0),
                        static_cast<unsigned long long>(c1), static_cast<double>(app), static_cast<double>(grMax),
                        static_cast<double>(mn), static_cast<double>(grMin), static_cast<double>(tg0),
                        static_cast<double>(tgtMax));

            std::vector<float> cpEdges, histEdges;
            for (const funkgui::Prim* p : tagged(cl, ui::tag::cpTarget))
                cpEdges.push_back(p->x0);
            for (const funkgui::Prim* p : tagged(cl, ui::tag::histGr))
                histEdges.push_back(p->x0);
            P.eq("truth.cp.hist_aligned", !cpEdges.empty() && cpEdges == histEdges ? 1 : 0, 1);
        }

        // The history internal's newest point, and READOUTS rows 11–18, against UiFrame.internals.
        const std::vector<funkgui::A11yItem> items = host.accessibility();
        const uint32_t rbase = ui::plotIdBase(ui::ViewIndex::charScreen, 3);
        const auto rowValue = [&](std::size_t row) -> std::string {  // 0-based row
            for (const funkgui::A11yItem& it : items)
                if (it.id == rbase + 2 + static_cast<uint32_t>(row) && it.visible)
                    return it.value;
            return "<no row>";
        };
        const auto text = [](float v, int dp, const char* unit) {
            char b[40];
            if (funkgui::fmt::db(v, std::clamp(dp, 0, 6), b, sizeof b) < 0)
                return std::string("\xE2\x80\x93");
            std::string s = b;
            if (unit != nullptr && unit[0] != '\0' && std::isfinite(v))
                s += std::string(" ") + unit;
            return s;
        };
        for (std::size_t i = 0; i < desc.internals.size() && i < 8; ++i)
        {
            const fcdsp::InternalSpec& in = desc.internals[i];
            const std::string want = text(f.ui.internals[i], in.decimals, in.unit);
            const std::string got = rowValue(layout::readouts::kFirstInternalRow + i);
            P.eq("truth.readouts.internal." + std::to_string(i), got == want ? 1 : 0, 1);
            if (got != want)
                std::printf("NOTE     ui.truth: READOUTS %s: '%s', want '%s'\n", in.name, got.c_str(), want.c_str());
            if (!in.history)
                continue;
            const funkgui::Rect& il = layout::kControlPath.internalLane;
            float y = -1.0f, xMax = -1.0f;
            for (const funkgui::Prim* p : tagged(cl, ui::tag::cpInternal))
                if (const Col c = colOf(*p); p->d2[2] > 2.5f && c.x1 > xMax)   // AREA prims (text is its label)
                {
                    xMax = c.x1;
                    y = c.top1;
                }
            const float v = std::clamp((f.ui.internals[i] - in.lo) / (in.hi - in.lo), 0.0f, 1.0f);
            P.le("truth.cp.internal_px", y < 0.0f ? 99.0 : std::fabs(y - (il.bottom() - v * il.h)), 0.5);
            std::printf("NOTE     ui.truth: history internal %s: frame %.4f, newest point %.3f px from the frame's\n",
                        in.name, static_cast<double>(f.ui.internals[i]),
                        y < 0.0f ? 99.0 : static_cast<double>(y - (il.bottom() - v * il.h)));
        }
        P.eq("truth.readouts.det", rowValue(0) == text(cx, 1, nullptr) ? 1 : 0, 1);
        P.eq("truth.readouts.target", rowValue(2) == text(std::max(tgt, 0.0f), 1, nullptr) ? 1 : 0, 1);
        P.eq("truth.readouts.applied", rowValue(3) == text(gr, 1, nullptr) ? 1 : 0, 1);
        std::printf("NOTE     ui.truth: READOUTS DET '%s' TARGET '%s' APPLIED '%s'\n", rowValue(0).c_str(),
                    rowValue(2).c_str(), rowValue(3).c_str());
        writePng(P, host, fl.pngChars);
    }

    // ---- HistoryStore and HISTORY over scripted columns -------------------------------------------------------------------

    struct Script
    {
        probe::FakeFacade& facade;
        uint8_t slot;
        fcdsp::UiFrame frame;

        void publish() { facade.publish(frame); }
        void columns(int n, uint8_t modeSlot, float inDb, float grDb, uint32_t phase)
        {
            for (int i = 0; i < n; ++i)
            {
                fcdsp::HistoryColumn c{};
                c.inPeakDb = inDb + static_cast<float>(i % 7);
                c.outPeakDb = inDb - grDb;
                c.detMaxDb = inDb;
                c.grMaxDb = grDb;
                c.grMinDb = grDb * 0.5f;
                c.tgtMaxDb = grDb;
                c.bits = (phase & 3u) | (static_cast<uint32_t>(modeSlot) << 8);
                facade.pushColumn(c);
            }
        }
    };

    int breaks(const std::vector<const funkgui::Prim*>& cols)
    {
        std::vector<Col> v;
        for (const funkgui::Prim* p : cols)
            v.push_back(colOf(*p));
        std::sort(v.begin(), v.end(), [](const Col& a, const Col& b) { return a.x0 < b.x0; });
        int n = 0;
        for (std::size_t i = 1; i < v.size(); ++i)
            if (v[i].x0 > v[i - 1].x1 + 0.01f)
                ++n;
        return n;
    }

    void historyRows(Probe& P, const fcdsp::ModeEntry& entry)
    {
        const fcdsp::ModeDescriptor& desc = *entry.desc;
        probe::FakeFacade facade(desc.key);
        fcdsp::Resolution res;
        fcdsp::resolve(entry, facade.currentRaw(), res);
        const auto slot = static_cast<uint8_t>(fcdsp::slotOf(entry));
        Script s{ facade, slot, probe::FakeFacade::quietFrame(slot, res.eng) };
        s.frame.flags |= fcdsp::kUiLive;
        ui::Panel panel(facade, kOpts);
        funkgui::HeadlessHost host(panel, 0, 1.0f);
        const ui::PanelContext& ctx = panel.context();
        const layout::HistoryGeom& hg = layout::kBandHistory;

        // Live, 1 s of columns, drained frame by frame.
        for (int k = 0; k < 60; ++k)
        {
            s.columns(17, slot, -20.0f, 3.0f, 1u);
            s.publish();
            host.tick(1, kDt);
        }
        // A lap: 5000 columns between two drains.
        s.columns(5000, slot, -20.0f, 3.0f, 3u);
        s.publish();
        host.tick(1, kDt);
        s.columns(200, slot, -12.0f, 6.0f, 1u);
        s.publish();
        host.tick(1, kDt);
        {
            const funkgui::PrimList& pl = host.draw();
            P.eq("history.lap.gap_markers", ctx.history.gaps(), 1);
            P.eq("history.lap.gap_drawn", tagged(pl, ui::tag::gap).empty() ? 0 : 1, 1);
            P.eq("history.lap.in_broken", breaks(tagged(pl, ui::tag::histIn)) >= 1 ? 1 : 0, 1);
            P.eq("history.lap.gr_broken", breaks(tagged(pl, ui::tag::histGr)) >= 1 ? 1 : 0, 1);
            const int lanes = static_cast<int>(tagged(pl, ui::tag::stateLane).size());
            P.in("history.state_lane", lanes, 1, layout::band::kStateLaneMaxRuns);
        }

        // A Mode boundary 100 ms before now.
        const auto other = static_cast<uint8_t>(slot == 0 ? 1 : 0);
        s.columns(300, other, -20.0f, 2.0f, 1u);
        s.columns(100, slot, -20.0f, 2.0f, 1u);
        s.publish();
        host.tick(1, kDt);
        {
            const funkgui::PrimList& pl = host.draw();
            const std::vector<const funkgui::Prim*> ticks = tagged(pl, ui::tag::modeTick);
            const double pxPerMs = static_cast<double>(hg.colWidth) * static_cast<double>(hg.columns)
                                 / (static_cast<double>(ctx.historySpanTenths) * 100.0);
            const auto want = static_cast<float>(static_cast<double>(hg.plot.right()) - 100.0 * pxPerMs);
            float best = 99.0f;
            for (const funkgui::Prim* p : ticks)
                best = std::min(best, std::fabs(boxOf(*p).x - want));
            P.eq("history.mode_tick.found", ticks.empty() ? 0 : 1, 1);
            P.le("history.mode_tick.x_px", best, 1.0);
        }

        // Stale (UF1a, ADR-69): no publish for > 0.5 s; the strip keeps scrolling at wall-clock rate over a gap.
        const auto dataEnd = [&](const funkgui::PrimList& pl) {
            float x = -1.0f;
            for (const funkgui::Prim* p : tagged(pl, ui::tag::histIn))
                x = std::max(x, colOf(*p).x1);
            return x;
        };
        const auto gapTo = [&](const funkgui::PrimList& pl) {
            float x = -1.0f;
            for (const funkgui::Prim* p : tagged(pl, ui::tag::gap))
                x = std::max(x, p->x1);
            return x;
        };
        host.tick(40, kDt);
        const float end0 = dataEnd(host.draw());
        host.tick(1, kDt);
        const funkgui::PrimList& sl = host.draw();
        const float end1 = dataEnd(sl);
        const double pxPerMsNow = static_cast<double>(hg.colWidth) * static_cast<double>(hg.columns)
                                / (static_cast<double>(ctx.historySpanTenths) * 100.0);
        P.near("history.stale_scrolls_px", end0 - end1, static_cast<double>(kDt) * 1000.0 * pxPerMsNow, 0.15);
        P.eq("history.stale_gap", !ctx.frame.fresh && gapTo(sl) >= hg.plot.right() - 2.0f ? 1 : 0, 1);
        // The audio returns: its columns land at "now", after the gap.
        s.columns(40, slot, -20.0f, 2.0f, 1u);
        s.publish();
        host.tick(1, kDt);
        {
            const funkgui::PrimList& rl = host.draw();
            P.eq("history.resume.lands_now", ctx.frame.fresh && std::fabs(dataEnd(rl) - hg.plot.right()) < 0.01f
                                                 && breaks(tagged(rl, ui::tag::histIn)) >= 1 ? 1 : 0, 1);
        }

        // Press and hold: the column under the pointer (the display row reads PanelContext::freeze).
        s.publish();
        host.tick(1, kDt);
        const float x = hg.plot.x + 200.0f, y = hg.plot.y + 40.0f;
        funkgui::PointerEvent e;
        e.x = x;
        e.y = y;
        panel.pointerMove(e);
        panel.pointerDown(e);
        host.tick(1, kDt);
        const float span = static_cast<float>(ctx.historySpanTenths) / 10.0f;
        P.eq("history.freeze.active", ctx.freeze.active ? 1 : 0, 1);
        P.near("history.freeze.at_s", ctx.freeze.atSeconds, -(hg.plot.right() - x) * span / hg.plot.w, 0.02);
        panel.pointerUp(e);
        host.tick(1, kDt);
        P.eq("history.freeze.released", ctx.freeze.active ? 0 : 1, 1);

        // The span cells write the preference (sandboxed) and the context mirrors it.
        const funkgui::Rect cell = hg.spanCells[2];                   // "10"
        host.click(cell.centreX(), cell.centreY());
        host.tick(1, kDt);
        P.eq("history.span.click", ctx.historySpanTenths == 100
                                        && funkgui::UiPreferences::get().getInt("historySpanTenths", 0, 0, 1000) == 100
                                    ? 1 : 0, 1);
        host.click(hg.spanCells[1].centreX(), hg.spanCells[1].centreY());   // back to the default
        host.tick(1, kDt);
    }
}

FCMP_PROBE(ui, truth)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;               // FontService bakes the atlas through JUCE's fonts
    const char* prefsDir = std::getenv("FCMP_PREFS_DIR");
    if (prefsDir == nullptr || *prefsDir == '\0')
    {
        P.harnessError("ui.truth writes UiPreferences: set FCMP_PREFS_DIR to a sandbox (CTest does)");
        return P.finish();
    }
    const fcdsp::ModeEntry* entry = fcdsp::byKey(C.key);
    if (entry == nullptr || entry->desc == nullptr)
    {
        P.harnessError("ui.truth: unknown Mode '" + std::string(C.key) + "'");
        return P.finish();
    }
    funkgui::UiPreferences::get().setInt("historySpanTenths", layout::kDefaultSpanTenths);
    funkgui::UiPreferences::get().setInt("meterScaleDb", layout::kDefaultScaleDb);
    funkgui::UiPreferences::get().setInt("grView", layout::vu::kHistory);   // UF2: the band shows HISTORY's traces
    engineRows(P, *entry, flags());
    historyRows(P, *entry);
    return P.finish();
}
