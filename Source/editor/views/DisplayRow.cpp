// Source/editor/views/DisplayRow.cpp — the display row (see DisplayRow.h): the big readout and its sub-readout, the
// OUTPUT trim, the QUALITY and LOOKAHEAD cells, and the DELTA, BYPASS and CHARACTERISTICS latches.
#include "editor/views/DisplayRow.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/SlotModel.h"
#include "editor/Tags.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/engine/Oversampler.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/HistoryRing.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/text/TextFit.h>
#include <funkgui/widgets/FocusRing.h>
#include <funkgui/widgets/ValueModel.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fcmp::ui
{
    namespace
    {
        namespace D = layout::display;
        namespace T = funkgui::type;

        // a11y locals (a11yId(ViewIndex::displayRow, local)); a SegmentedSelector's cells are its id + 1 + i.
        constexpr uint32_t kReadoutLocal = 1;
        constexpr uint32_t kQualityLocal = 16;
        constexpr uint32_t kBudgetLocal  = 32;
        constexpr uint32_t kDeltaLocal   = 48;
        constexpr uint32_t kBypassLocal  = 49;
        constexpr uint32_t kCharsLocal   = 50;
        constexpr uint32_t kOutputLocal  = 64;                   // v1.2 (ADR-88)

        constexpr float kValueMaxW = D::kSubX - 8.0f - D::kValue.x;   // value + unit end 8 px before the sub-readout
        constexpr float kUnitGap   = 6.0f;                               // value, then unit (HR, RuleSlider)
        constexpr float kLevelFloorDb = -99.95f;                         // quieter prints −∞ (the meters floor at −200)
        constexpr float kSilentDb = telemetry::kFloorDb;                  // the telemetry floor: nothing arrived
        constexpr funkgui::Point kNowhere { -1.0e6f, -1.0e6f };          // "the pointer is not over this row"

        constexpr const char* kSep  = " \xC2\xB7 ";                       // " · "
        constexpr const char* kDash = "\xE2\x80\x93";                     // "–": not live / n/a (01 §4.6)

        constexpr std::array<const char*, 3> kQualitySpoken { "Eco", "Std", "HQ" };
        constexpr std::array<const char*, 3> kQualityFamily { "", "IIR", "FIR" };   // Oversampler.h: the STD / HQ designs
        constexpr std::array<const char*, 3> kBudgetSpoken  { "Off", "5 ms", "20 ms" };

        constexpr const char* kDeltaSpec =
            "DELTA   HEAR ONLY WHAT THE COMPRESSOR REMOVES   A MONITORING LATCH: A SESSION LOAD TURNS IT OFF";
        constexpr const char* kBypassSpec =
            "BYPASS   20 MS CROSSFADE TO THE DRY SIGNAL   THE LATENCY STAYS THE SAME";
        constexpr const char* kCharsSpec =
            "CHARACTERISTICS   CURVES, TIMING AND SIDECHAIN IN DETAIL   ESC RETURNS";

        bool isModeParam(fcdsp::Pid p) noexcept { return p != fcdsp::kNoPid && fcdsp::idx(p) < fcdsp::kNumModeParams; }

        // Appends UTF-8 text to a fixed buffer, never cutting inside a codepoint.
        struct Line
        {
            Line(char* o, std::size_t c) noexcept : out(o), cap(c)
            {
                if (cap > 0)
                    out[0] = '\0';
            }

            char*       out;
            std::size_t cap;
            std::size_t n = 0;

            void add(std::string_view s) noexcept
            {
                for (const char ch : s)
                {
                    if (n + 1 >= cap)
                    {
                        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0u) == 0x80u)
                            --n;
                        break;
                    }
                    out[n++] = ch;
                }
                out[n] = '\0';
            }
            void add(const char* s) noexcept { add(std::string_view(s != nullptr ? s : "")); }
        };

        // A level in dBFS with one decimal; below the floor "−∞".
        void addLevel(Line& line, float db) noexcept
        {
            char buf[24];
            if (!(db > kLevelFloorDb))
                funkgui::fmt::db(-INFINITY, 1, buf, sizeof buf);
            else
                funkgui::fmt::db(db, 1, buf, sizeof buf);
            line.add(buf);
        }

        // A Mode-filtered parameter's host-plain value in its universal unit (kHostParams): the renamed slot's
        // "THRESHOLD −28.0 DB", CLAMPED FROM …, STORED ….
        void addUniversal(Line& line, fcdsp::Pid pid, float plain) noexcept
        {
            const fcdsp::HostParam& h = fcdsp::kHostParams[fcdsp::idx(pid)];
            char buf[24];
            const char* unit = nullptr;
            switch (h.map)
            {
                case fcdsp::Map::boolean:
                    line.add(plain >= 0.5f ? "ON" : "OFF");
                    return;
                case fcdsp::Map::index:
                {
                    const auto step = static_cast<int>(std::lround(plain));
                    line.add("STEP ");
                    const char digit[2] { static_cast<char>('0' + std::clamp(step, 0, 9)), '\0' };
                    line.add(digit);
                    return;
                }
                case fcdsp::Map::ratio3:
                    if (std::fabs(plain - 1.0f) < 1.0e-6f)
                        line.add("\xE2\x88\x9E:1");                       // ∞:1
                    else
                    {
                        funkgui::fmt::db(1.0f / (1.0f - plain), 1, buf, sizeof buf);
                        line.add(buf);
                        line.add(":1");
                    }
                    return;
                case fcdsp::Map::linear:
                case fcdsp::Map::log:
                case fcdsp::Map::power:
                    break;
            }
            const std::string_view u(h.unit != nullptr ? h.unit : "");
            if (u == "MS")
            {
                funkgui::fmt::seconds(plain * 1.0e-3f, buf, sizeof buf, &unit);
                line.add(buf);
                line.add(" ");
                line.add(unit);
            }
            else if (u == "HZ")
            {
                if (plain < 20.0f)
                    line.add("OFF");                                     // the SC filter is off below 20 Hz (01 §4.3)
                else
                {
                    funkgui::fmt::hz(plain, buf, sizeof buf, &unit);
                    line.add(buf);
                    line.add(std::string_view(unit) == "K" ? " KHZ" : " HZ");
                }
            }
            else if (u == "%")
            {
                funkgui::fmt::percent(plain, buf, sizeof buf);
                line.add(buf);
                line.add(" %");
            }
            else
            {
                funkgui::fmt::db(plain, 1, buf, sizeof buf);             // DB, DB/OCT
                line.add(buf);
                if (!u.empty())
                {
                    line.add(" ");
                    line.add(u);
                }
            }
        }

        std::string countText(int n, const char* one, const char* many)
        {
            return std::to_string(n) + (n == 1 ? one : many);
        }

        std::array<std::string, 3> qualityHelps()
        {
            std::array<std::string, 3> out;
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                const fcdsp::OsDesign& d = fcdsp::kOs[i];
                std::string s = std::string(kQualitySpoken[i]) + ": ";
                s += d.factor <= 1 ? std::string("no oversampling")
                                   : std::to_string(d.factor) + " times " + kQualityFamily[i] + " oversampling";
                s += ", " + countText(d.latency, " sample", " samples") + " latency";
                out[i] = std::move(s);
            }
            return out;
        }

        std::string qualitySpecLine()
        {
            std::string s = "QUALITY  ";
            const auto& choices = fcdsp::kHostParams[fcdsp::idx(fcdsp::Pid::quality)].choices;
            for (std::size_t i = 0; i < 3; ++i)
            {
                const fcdsp::OsDesign& d = fcdsp::kOs[i];
                s += std::string(i == 0 ? " " : kSep) + choices[i] + " ";
                s += d.factor <= 1 ? std::string("NO OVERSAMPLING")
                                   : std::to_string(d.factor) + "\xC3\x97 " + kQualityFamily[i] + ", "
                                         + countText(d.latency, " SAMPLE", " SAMPLES");
            }
            return s + "   OVERSAMPLING ADDS LATENCY";
        }

        std::string msText(float ms) { return std::to_string(static_cast<int>(std::lround(ms))); }

        std::array<std::string, 3> budgetHelps()
        {
            std::array<std::string, 3> out;
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                const float ms = fcdsp::budgetMs(static_cast<fcdsp::LookaheadBudget>(i));
                out[i] = ms > 0.0f ? std::string(kBudgetSpoken[i]) + ": up to " + msText(ms)
                                         + " ms of lookahead, adds " + msText(ms) + " ms of latency"
                                   : std::string(kBudgetSpoken[i]) + ": no lookahead, no added latency";
            }
            return out;
        }

        std::string budgetSpecLine()
        {
            std::string s = "LOOKAHEAD   BUDGET";
            const auto& choices = fcdsp::kHostParams[fcdsp::idx(fcdsp::Pid::labudget)].choices;
            for (std::size_t i = 0; i < 3; ++i)
                s += std::string(i == 0 ? " " : kSep) + choices[i];
            return s + "   THE BUDGET IS ADDED AS LATENCY   THE LOOKAHEAD SLOT USES UP TO IT";
        }

        std::vector<funkgui::CellText> cellTexts(fcdsp::Pid pid, const std::array<const char*, 3>& spoken,
                                                 const std::array<std::string, 3>& help)
        {
            std::vector<funkgui::CellText> t;
            const auto& choices = fcdsp::kHostParams[fcdsp::idx(pid)].choices;
            for (std::size_t i = 0; i < 3; ++i)
                t.push_back({ choices[i], spoken[i], help[i].c_str() });
            return t;
        }
    }

    // ---- the CHARACTERISTICS latch --------------------------------------------------------------------------------------

    bool DisplayRow::CharsModel::on() const { return ctx_.screen == Screen::characteristics; }

    void DisplayRow::CharsModel::set(bool on, funkgui::GestureController&)
    {
        // UI state, not a parameter: Panel::setView eases the screen and writes UiState::charExpanded (02 §7.1).
        ctx_.panel.setView({ nullptr, on ? Screen::characteristics : Screen::panel, ctx_.scTab, ctx_.overlay }, false);
    }

    // ---- construction ---------------------------------------------------------------------------------------------------

    DisplayRow::DisplayRow(PanelContext& ctx)
        : ctx_(ctx),
          qualityHelp_(qualityHelps()),
          budgetHelp_(budgetHelps()),
          qualitySpec_(qualitySpecLine()),
          budgetSpec_(budgetSpecLine()),
          qualityModel_(ctx.facade.port(fcdsp::Pid::quality), cellTexts(fcdsp::Pid::quality, kQualitySpoken, qualityHelp_)),
          budgetModel_(ctx.facade.port(fcdsp::Pid::labudget), cellTexts(fcdsp::Pid::labudget, kBudgetSpoken, budgetHelp_)),
          deltaModel_(ctx.facade.port(fcdsp::Pid::delta)),
          bypassModel_(ctx.facade.port(fcdsp::Pid::bypass)),
          charsModel_(ctx),
          quality_(qualityModel_, std::vector<funkgui::Rect>(D::kQualityCells.begin(), D::kQualityCells.end()),
                   funkgui::CellStyle::text,
                   "QUALITY", D::kQualityCaption, a11yId(ViewIndex::displayRow, kQualityLocal)),
          budget_(budgetModel_, std::vector<funkgui::Rect>(D::kLookaheadCells.begin(), D::kLookaheadCells.end()),
                  funkgui::CellStyle::text,
                  "LOOKAHEAD", D::kLookaheadCaption, a11yId(ViewIndex::displayRow, kBudgetLocal)),
          delta_(deltaModel_, D::kDelta, "DELTA", a11yId(ViewIndex::displayRow, kDeltaLocal)),
          bypass_(bypassModel_, D::kBypass, "BYPASS", a11yId(ViewIndex::displayRow, kBypassLocal)),
          chars_(charsModel_, D::kCharacteristics, "CHARACTERISTICS", a11yId(ViewIndex::displayRow, kCharsLocal)),
          output_(ctx, a11yId(ViewIndex::displayRow, kOutputLocal))
    {
        quality_.setSpokenTitle("Quality");
        budget_.setSpokenTitle("Lookahead budget");
        activity_ = activityOf(ctx);                             // the state at birth is no input
    }

    // ---- state ----------------------------------------------------------------------------------------------------------

    funkgui::Point DisplayRow::pointerForWidgets() const noexcept
    {
        return pointerOver_ || captured_ >= 0 ? ctx_.pointer : kNowhere;
    }

    int DisplayRow::widgetAt(funkgui::Point p) const noexcept
    {
        if (output_.contains(p))
            return wOutput;
        if (quality_.contains(p))
            return wQuality;
        if (budget_.contains(p))
            return wBudget;
        if (delta_.contains(p))
            return wDelta;
        if (bypass_.contains(p))
            return wBypass;
        if (chars_.contains(p))
            return wChars;
        return -1;
    }

    int DisplayRow::widgetOf(uint32_t id) const noexcept
    {
        const auto inGroup = [id](const funkgui::SegmentedSelector& s) {
            return id >= s.a11yId() && id <= s.a11yId() + 3u;    // the group and its three cells
        };
        if (inGroup(quality_))
            return wQuality;
        if (inGroup(budget_))
            return wBudget;
        if (id == delta_.a11yId())
            return wDelta;
        if (id == bypass_.a11yId())
            return wBypass;
        if (id == chars_.a11yId())
            return wChars;
        if (id == output_.a11yId())
            return wOutput;
        return -1;
    }

    DisplayRow::Activity DisplayRow::activityOf(const PanelContext& ctx) noexcept
    {
        Activity a;
        a.x = ctx.pointer.x;
        a.y = ctx.pointer.y;
        a.moves = ctx.pointerMoves;
        a.downs = ctx.pointerDowns;
        a.focus = ctx.focus;
        a.handItem = ctx.hand.item;
        a.touchedAt = ctx.touchedAt;
        a.handKind = ctx.hand.kind;
        a.screen = ctx.screen;
        a.overlay = ctx.overlay;
        a.scTab = ctx.scTab;
        a.in = ctx.pointerIn;
        a.pressed = ctx.pointerPressed;
        a.focusVisible = ctx.focusVisible;
        return a;
    }

    // ADR-70: the GAIN REDUCTION hold (refreshed at ≈ 4 Hz of panel time) and the live GR bar; ADR-69: with no frame
    // arriving the bar falls at 20 dB/s, the hold empties within kGrHoldS and IN · OUT read −∞.
    void DisplayRow::tickTelemetry(float dt) noexcept
    {
        const telemetry::Feed fd = telemetry::feed(ctx_);
        if (fd == telemetry::Feed::none)
        {
            grHold_ = grBar_ = 0.0f;
            inShown_ = outShown_ = inAcc_ = outAcc_ = kSilentDb;
            refreshSlot_ = -1;
            seen_ = false;
            return;
        }
        const fcdsp::UiFrame& u = ctx_.frame.ui;
        if (fd == telemetry::Feed::fresh)
        {
            const float gr = std::max(u.appliedGrDb[0], u.appliedGrDb[1]);
            grBar_ = std::isfinite(gr) ? std::max(gr, 0.0f) : 0.0f;
            inAcc_ = std::max({ inAcc_, u.inPeakDb[0], u.inPeakDb[1] });
            outAcc_ = std::max({ outAcc_, u.outPeakDb[0], u.outPeakDb[1] });
        }
        else
        {
            grBar_ = std::max(0.0f, grBar_ - layout::meters::kFallDbPerS * std::max(dt, 0.0f));
        }
        // Quantised in time: the text changes at multiples of kGrRefreshS of panel time (and at once for the first frame).
        const auto slot = static_cast<int64_t>(std::floor(ctx_.seconds / static_cast<double>(D::kGrRefreshS)));
        if (!seen_ || slot != refreshSlot_)
        {
            grHold_ = telemetry::grHoldDb(ctx_);
            inShown_ = inAcc_;
            outShown_ = outAcc_;
            inAcc_ = outAcc_ = kSilentDb;
            refreshSlot_ = slot;
            seen_ = true;
        }
    }

    void DisplayRow::tick(float dt)
    {
        // Input activity: full rate for kActiveS after any change (DisplayRow.h).
        if (const Activity a = activityOf(ctx_); !(a == activity_))
        {
            activity_ = a;
            activeAt_ = ctx_.seconds;
        }
        tickTelemetry(dt);

        const funkgui::Point p = pointerForWidgets();
        quality_.tick(dt, p);
        budget_.tick(dt, p);
        delta_.tick(dt, p);
        bypass_.tick(dt, p);
        chars_.tick(dt, p);
        output_.tick(dt, captured_ == wOutput || (captured_ < 0 && widgetAt(p) == wOutput));

        // The readout's dwell (HR :983-987): the last dragged or hovered Mode parameter stays 0.9 s after the hand
        // leaves it. PanelContext::hand is the last completed tick's.
        const HandState& h = ctx_.hand;
        if (isModeParam(h.pid) && (h.kind == HandKind::drag || h.kind == HandKind::hover))
        {
            recentPid_ = h.pid;
            recentAt_ = ctx_.seconds;
        }

        // The item under the hand: its footer spec line (02 §6.6).
        const auto offer = [this](int w, HandKind kind) {
            switch (w)
            {
                case wQuality: ctx_.offerHand(fcdsp::Pid::quality, kind, quality_.a11yId(), qualitySpec_.c_str()); break;
                case wBudget:  ctx_.offerHand(fcdsp::Pid::labudget, kind, budget_.a11yId(), budgetSpec_.c_str()); break;
                case wDelta:   ctx_.offerHand(fcdsp::Pid::delta, kind, delta_.a11yId(), kDeltaSpec); break;
                case wBypass:  ctx_.offerHand(fcdsp::Pid::bypass, kind, bypass_.a11yId(), kBypassSpec); break;
                case wChars:   ctx_.offerHand(fcdsp::kNoPid, kind, chars_.a11yId(), kCharsSpec); break;
                case wOutput:  ctx_.offerHand(fcdsp::Pid::output, kind, output_.a11yId(), output_.spec()); break;
                default:       break;
            }
        };
        if (output_.dragging() || output_.entryOpen())
            offer(wOutput, HandKind::drag);
        else if (pointerOver_)
            offer(widgetAt(ctx_.pointer), HandKind::hover);
        if (ctx_.focusVisible)
            offer(widgetOf(ctx_.focus), HandKind::focus);
    }

    bool DisplayRow::wantsFullRate() const
    {
        const bool active = activeAt_ >= 0.0 && ctx_.seconds - activeAt_ < static_cast<double>(layout::live::kActiveS);
        const bool falling = grBar_ > 0.0f && telemetry::feed(ctx_) != telemetry::Feed::fresh;   // the bar falls
        return !quality_.settled() || !budget_.settled() || !delta_.settled() || !bypass_.settled()
            || !chars_.settled() || !output_.settled() || active || falling;
    }

    fcdsp::Pid DisplayRow::shownPid() const noexcept
    {
        if (ctx_.frame.entry == nullptr)
            return fcdsp::kNoPid;
        const HandState& h = ctx_.hand;
        if (isModeParam(h.pid) && h.kind == HandKind::drag)
            return h.pid;
        if (isModeParam(h.pid) && h.kind == HandKind::hover)
            return h.pid;
        const auto dwell = static_cast<double>(D::kDwellS);
        const bool recent = isModeParam(recentPid_) && recentAt_ >= 0.0 && ctx_.seconds - recentAt_ < dwell;
        const bool touched = isModeParam(ctx_.touched) && ctx_.touchedAt >= 0.0 && ctx_.seconds - ctx_.touchedAt < dwell;
        if (recent && touched)
            return recentAt_ >= ctx_.touchedAt ? recentPid_ : ctx_.touched;
        if (recent)
            return recentPid_;
        if (touched)
            return ctx_.touched;
        return fcdsp::kNoPid;
    }

    // ---- the readout ----------------------------------------------------------------------------------------------------

    void DisplayRow::readout(const funkgui::Theme& th, Readout& r) const
    {
        const FrameState& f = ctx_.frame;
        Line caption{ r.caption, sizeof r.caption };
        Line value{ r.value, sizeof r.value };
        Line unit{ r.unit, sizeof r.unit };
        Line sub{ r.sub, sizeof r.sub };
        Line spoken{ r.spoken, sizeof r.spoken };
        r.subInk = th.ink32;

        // The HISTORY freeze column (02 §6.5).
        if (ctx_.freeze.active)
        {
            const fcdsp::HistoryColumn& col = ctx_.freeze.column;
            char at[24];
            funkgui::fmt::db(ctx_.freeze.atSeconds, 2, at, sizeof at);
            caption.add("GAIN REDUCTION");
            caption.add(kSep);
            caption.add("AT ");
            caption.add(at);
            caption.add(" S");
            const float gr = col.grMaxDb;
            if (gr > D::kGrShownDb)
                addLevel(value, -gr);
            else
                value.add("0.0");
            unit.add("DB");
            r.valueInk = gr > D::kGrShownDb ? th.signal : th.ink32;
            sub.add("IN ");
            addLevel(sub, col.inPeakDb);
            sub.add(kSep);
            sub.add("OUT ");
            addLevel(sub, col.outPeakDb);
            r.live = true;
            spoken.add("Gain reduction at ");
            spoken.add(at);
            spoken.add(" s, ");
            spoken.add(r.value);
            spoken.add(" dB");
            return;
        }

        // A slot: the dragged, hovered or recently touched one (02 §6.6).
        if (const fcdsp::Pid pid = shownPid(); pid != fcdsp::kNoPid)
        {
            const SlotModel& slot = ctx_.slot(pid);
            funkgui::ValueView v;
            slot.view(v);
            caption.add(v.label);
            switch (v.state)
            {
                case funkgui::ValueState::na:      r.valueInk = th.ink16; break;
                case funkgui::ValueState::locked:  r.valueInk = th.ink32; break;
                case funkgui::ValueState::derived: r.valueInk = th.ink52; break;
                case funkgui::ValueState::continuous:
                case funkgui::ValueState::stepped: r.valueInk = th.ink100; break;
            }
            if (v.state == funkgui::ValueState::na || v.text.value[0] == '\0')
                value.add(kDash);
            else
            {
                value.add(v.text.value);
                unit.add(v.text.unit);
            }
            const fcdsp::ResolvedParam& res = slot.resolved();
            const float raw = f.raw.v[fcdsp::idx(pid)];
            const bool refused = v.state == funkgui::ValueState::locked || v.state == funkgui::ValueState::na;
            if ((res.flags & fcdsp::kClamped) != 0)
            {
                sub.add("CLAMPED FROM ");
                addUniversal(sub, pid, raw);
            }
            else if (v.aka != nullptr && v.state != funkgui::ValueState::na)
            {
                sub.add(v.aka);
                sub.add(" ");
                addUniversal(sub, pid, res.plain);
            }
            else if (refused && raw != res.plain)
            {
                sub.add("STORED ");
                addUniversal(sub, pid, raw);
            }
            spoken.add(v.label);
            spoken.add(", ");
            spoken.add(v.text.spoken[0] != '\0' ? v.text.spoken : (v.state == funkgui::ValueState::na ? "not applicable" : v.text.value));
            return;
        }

        // GAIN REDUCTION (02 §6.6; ADR-70): the 1 s hold, signal above 0.05 dB, 0.0 at zero; ADR-69: "–" only before the
        // first frame ever — once the audio stops it rests at 0.0 (the hold empties), IN · OUT at −∞.
        caption.add("GAIN REDUCTION");
        spoken.add("Gain reduction, ");
        if (seen_ && telemetry::feed(ctx_) != telemetry::Feed::none)
        {
            const float gr = grHold_;
            if (gr > D::kGrShownDb)
            {
                addLevel(value, -gr);
                r.valueInk = th.signal;
            }
            else
            {
                value.add("0.0");
                r.valueInk = th.ink32;
            }
            unit.add("DB");
            sub.add("IN ");
            addLevel(sub, inShown_);
            sub.add(kSep);
            sub.add("OUT ");
            addLevel(sub, outShown_);
            r.live = true;
            spoken.add(r.value);
            spoken.add(" dB");
        }
        else
        {
            value.add(kDash);
            r.valueInk = th.ink16;
            sub.add("IN ");
            sub.add(kDash);
            sub.add(kSep);
            sub.add("OUT ");
            sub.add(kDash);
            r.subInk = th.ink16;
            spoken.add("no signal");
        }
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    void DisplayRow::drawBypassRamp(funkgui::Canvas& c, const funkgui::Theme& th, float amount) const
    {
        // 02 §9.1: while the 20 ms bypass ramp runs, the latch's ink70 fill covers bypassAmt of its width.
        const funkgui::Rect& r = bypass_.bounds();
        const funkgui::Canvas::Scope scope(c, funkgui::tags::latch, true);
        c.rrect(r.x, r.y, r.w, r.h, 0.0f, th.ink16);
        c.rrect(r.x, r.y, r.w * amount, r.h, 0.0f, th.ink70);
        c.text("BYPASS", r.centreX(), c.capCentreTop(r.centreY(), T::kLatch), T::kLatch,
               amount >= 0.5f ? th.ground : th.ink52, funkgui::Align::centre);
        if (ctx_.focusVisible && ctx_.focus == bypass_.a11yId())
            funkgui::drawFocusRing(c, r, th.accent);
    }

    void DisplayRow::drawGrBar(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        // ADR-70: the live GR bar beside the value, on the band's GR scale (4 px/dB at the default 48 dB), with the held
        // readout's tick; the track is chrome (static), the bar and tick telemetry (live).
        const funkgui::Rect& b = D::kGrBar;
        {
            const funkgui::Canvas::Scope scope(c, tag::displayValue, false);
            c.hairlineH(b.x, D::kGrTrackY, b.w, th.ink16);
        }
        if (!seen_ || telemetry::feed(ctx_) == telemetry::Feed::none)
            return;
        const float ppd = layout::kBandTransfer.level.pxPerDb(static_cast<float>(ctx_.meterScaleDb));
        const funkgui::Canvas::Scope scope(c, tag::displayValue, true);
        if (const float len = std::clamp(grBar_ * ppd, 0.0f, b.w); len >= 0.5f)
            c.rrect(b.x, b.y, len, b.h, 0.0f, th.signal);
        if (grHold_ > D::kGrShownDb)
            c.hairlineV(b.x + std::clamp(grHold_ * ppd, 0.0f, b.w - 1.0f), D::kGrTickTop, D::kGrTickH, th.ink100);
    }

    void DisplayRow::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        Readout r;
        readout(th, r);
        {
            const funkgui::Canvas::Scope scope(c, tag::displayCaption, false);
            c.text(r.caption, D::kCaption.x, D::kCaption.y, T::kCaption, th.ink52);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::displayValue, r.live);
            const float unitW = r.unit[0] != '\0' ? c.textWidth(r.unit, T::kUnit) + kUnitGap : 0.0f;
            const funkgui::TextStyle* style = &T::kDisplay;
            float top = D::kValue.y;
            char fitted[64];
            std::memcpy(fitted, r.value, sizeof fitted);
            if (c.textWidth(r.value, T::kDisplay) + unitW > kValueMaxW)
            {
                style = &T::kValueP;                             // too wide at kDisplay: the slot's primary size
                top = c.sharedBaselineTop(D::kValue.y, T::kDisplay, T::kValueP);
                funkgui::text::fitEllipsis(ctx_.atlas, r.value, T::kValueP, kValueMaxW - unitW, fitted, sizeof fitted);
            }
            c.text(fitted, D::kValue.x, top, *style, r.valueInk);
            if (r.unit[0] != '\0')
            {
                const float x = D::kValue.x + c.textWidth(fitted, *style) + kUnitGap;
                c.text(r.unit, x, c.sharedBaselineTop(D::kValue.y, T::kDisplay, T::kUnit), T::kUnit, th.ink52);
            }
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::displaySub, r.live);
            char sub[96];
            funkgui::text::fitEllipsis(ctx_.atlas, r.sub, T::kLabel, D::kSubMaxW, sub, sizeof sub);
            c.text(sub, D::kSubX, c.capCentreTop(D::kSubCentreY, T::kLabel), T::kLabel, r.subInk);
        }
        if (!ctx_.freeze.active && shownPid() == fcdsp::kNoPid)
            drawGrBar(c, th);                                    // only beside GAIN REDUCTION

        const auto focused = [this](uint32_t id) { return ctx_.focusVisible && ctx_.focus == id; };
        output_.draw(c, th, focused(output_.a11yId()));
        quality_.draw(c, th, focused(quality_.a11yId()));
        budget_.draw(c, th, focused(budget_.a11yId()));
        delta_.draw(c, th, focused(delta_.a11yId()));
        const float amt = ctx_.frame.fresh ? ctx_.frame.ui.bypassAmt : 0.0f;
        if (amt > 0.0f && amt < 1.0f)
            drawBypassRamp(c, th, amt);
        else
            bypass_.draw(c, th, focused(bypass_.a11yId()));
        chars_.draw(c, th, focused(chars_.a11yId()));
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    bool DisplayRow::hit(funkgui::Point p) const { return layout::kDisplayRow.contains(p); }

    void DisplayRow::pointerMove(const funkgui::PointerEvent&) { pointerOver_ = true; }

    void DisplayRow::pointerExit() { pointerOver_ = false; }

    void DisplayRow::pointerDown(const funkgui::PointerEvent& e)
    {
        pointerOver_ = true;
        captured_ = widgetAt({ e.x, e.y });
        if (ctx_.gestures == nullptr)
            return;
        funkgui::GestureController& g = *ctx_.gestures;
        switch (captured_)
        {
            case wOutput:  output_.pointerDown(e, g); break;      // a drag is one gesture
            case wQuality: quality_.pointerDown(e, g); break;     // selects on down (HR :1709)
            case wBudget:  budget_.pointerDown(e, g); break;
            case wDelta:   delta_.pointerDown(e, g); break;       // arms; commits on up inside
            case wBypass:  bypass_.pointerDown(e, g); break;
            case wChars:   chars_.pointerDown(e, g); break;
            default:       break;
        }
    }

    void DisplayRow::pointerDrag(const funkgui::PointerEvent& e)
    {
        switch (captured_)
        {
            case wOutput:
                if (ctx_.gestures != nullptr)
                    output_.pointerDrag(e, *ctx_.gestures);
                break;
            case wDelta:  delta_.pointerDrag(e); break;           // dragging off disarms
            case wBypass: bypass_.pointerDrag(e); break;
            case wChars:  chars_.pointerDrag(e); break;
            default:      break;
        }
    }

    void DisplayRow::pointerUp(const funkgui::PointerEvent& e)
    {
        const int w = captured_;
        captured_ = -1;
        if (ctx_.gestures == nullptr)
            return;
        funkgui::GestureController& g = *ctx_.gestures;
        switch (w)
        {
            case wOutput: output_.pointerUp(g); break;
            case wDelta:  delta_.pointerUp(e, g); break;
            case wBypass: bypass_.pointerUp(e, g); break;
            case wChars:  chars_.pointerUp(e, g); break;
            default:      break;
        }
    }

    bool DisplayRow::key(const funkgui::KeyEvent& e)
    {
        if (ctx_.gestures == nullptr)
            return false;
        funkgui::GestureController& g = *ctx_.gestures;
        if (output_.entryOpen())                                 // ADR-89: its field takes every key
            return output_.key(e, g);
        switch (widgetOf(ctx_.focus))
        {
            case wOutput:  return output_.key(e, g);
            case wQuality: return quality_.key(e, g);
            case wBudget:  return budget_.key(e, g);
            case wDelta:   return delta_.key(e, g);
            case wBypass:  return bypass_.key(e, g);
            case wChars:   return chars_.key(e, g);
            default:       return false;
        }
    }

    funkgui::Cursor DisplayRow::cursor(funkgui::Point p) const
    {
        switch (widgetAt(p))
        {
            case wOutput:  return funkgui::Cursor::leftRight;
            case wQuality: return quality_.cursorAt(p);
            case wBudget:  return budget_.cursorAt(p);
            case wDelta:   return delta_.cursorAt(p);
            case wBypass:  return bypass_.cursorAt(p);
            case wChars:   return chars_.cursorAt(p);
            default:       return funkgui::Cursor::normal;
        }
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void DisplayRow::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        Readout r;
        readout(funkgui::Theme::graphite(), r);
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::displayRow, kReadoutLocal);
        it.role = funkgui::A11yRole::staticText;
        it.bounds = { D::kCaption.x, D::kCaption.y, D::kSubX + D::kSubMaxW - D::kCaption.x, 120.0f - D::kCaption.y };
        it.title = "Display";
        it.value = r.spoken;
        if (r.sub[0] != '\0')
            it.description = r.sub;
        it.readOnly = true;
        out.push_back(std::move(it));
        output_.accessibility(out);
        quality_.accessibility(out);
        budget_.accessibility(out);
        delta_.accessibility(out);
        bypass_.accessibility(out);
        chars_.accessibility(out);
    }

    int DisplayRow::focusOrder(std::span<uint32_t> out) const
    {
        const std::array<uint32_t, 6> stops { output_.a11yId(), quality_.a11yId(), budget_.a11yId(), delta_.a11yId(),
                                              bypass_.a11yId(), chars_.a11yId() };
        const std::size_t n = std::min(out.size(), stops.size());
        std::copy_n(stops.begin(), n, out.begin());
        return static_cast<int>(n);
    }

    void DisplayRow::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        if (ctx_.gestures == nullptr)
            return;
        funkgui::GestureController& g = *ctx_.gestures;
        switch (widgetOf(id))
        {
            case wQuality: quality_.a11yAction(id, a, value, g); break;
            case wBudget:  budget_.a11yAction(id, a, value, g); break;
            case wDelta:   delta_.a11yAction(id, a, g); break;
            case wBypass:  bypass_.a11yAction(id, a, g); break;
            case wChars:   chars_.a11yAction(id, a, g); break;
            case wOutput:  output_.a11yAction(a, value, g); break;
            default:       break;
        }
    }

    void DisplayRow::doubleClick(const funkgui::PointerEvent& e)
    {
        if (ctx_.gestures != nullptr && (captured_ == wOutput || (captured_ < 0 && widgetAt({ e.x, e.y }) == wOutput)))
            output_.reset(*ctx_.gestures);
    }

    void DisplayRow::endTextEntry(bool commit)
    {
        if (ctx_.gestures != nullptr)
            output_.endEntry(commit, *ctx_.gestures);
    }

    bool DisplayRow::takesTypedKeys(uint32_t id) const { return id == output_.a11yId(); }

    bool DisplayRow::wheel(const funkgui::WheelEvent& e)
    {
        if (ctx_.gestures == nullptr || ctx_.host == nullptr || widgetAt({ e.x, e.y }) != wOutput)
            return false;
        return output_.wheel(e, *ctx_.gestures, ctx_.host->nowSeconds());
    }
}
