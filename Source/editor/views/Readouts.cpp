// Source/editor/views/Readouts.cpp — READOUTS (see Readouts.h; 02 §7.3, §9.1): the eighteen rows' texts built in tick()
// from the frame (draw() only emits them), the L/R or M/S pairs, and one staticText a11y item per row.
#include "editor/views/Readouts.h"

#include "editor/HistoryStore.h"
#include "editor/Tags.h"

#include "fcdsp/analysis/Analysis.h"
#include "fcdsp/core/Units.h"
#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Format.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/text/TextFit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace T = funkgui::type;
        namespace R = layout::readouts;

        constexpr int   kRows = 18;                                // layout::kReadouts.rows (02 §7.3)
        constexpr int   kFixedRows = static_cast<int>(R::kNames.size());   // rows 1–10
        constexpr int   kMaxInternals = kRows - R::kFirstInternalRow;      // rows 11–18
        constexpr float kFloorDb = -199.0f;                        // telemetry floor (−200 dBFS) and below: "–"
        constexpr float kRatioInf = 1.0e4f;                        // |ratio| beyond this prints ∞:1 (SlotModel's EFF)
        constexpr float kNameGap = 6.0f;                           // the least room between a name and its value
        constexpr uint64_t kEnvelopeMs = 10;                       // TransferPlot's operating-point envelope (10 ms)
        constexpr uint32_t kImageId = 1, kFirstRowId = 2;          // local ids: rows kFirstRowId + i (i < kRows)
        constexpr const char* kDash = "\xE2\x80\x93";              // U+2013: not live / not applicable
        constexpr const char* kMiddot = " \xC2\xB7 ";              // " · "

        static_assert(kFixedRows == R::kFirstInternalRow && kMaxInternals == 8, "rows 1–10 fixed, 11–18 internals");

        // What rows 1–10 measure, for the a11y description.
        constexpr std::array<const char*, kFixedRows> kMeaning {
            "Detector level, decibels", "Level over the threshold, decibels", "Target gain reduction, decibels",
            "Applied gain reduction, decibels", "Stage 2 gain reduction, decibels", "Effective ratio at the detector level",
            "Effective attack time", "Effective release time", "Crest factor, decibels", "Envelope phase"
        };
        constexpr std::array<const char*, 4> kPhases { "IDLE", "ATTACK", "HOLD", "RELEASE" };

        enum Row : int { rDet, rOver, rTarget, rApplied, rS2, rRatio, rAtk, rRel, rCrest, rPhase };

        // A bounded text builder (no allocation): appends while it fits, never splits a UTF-8 sequence it was given whole.
        template <std::size_t N>
        struct Text
        {
            char     s[N] {};
            std::size_t n = 0;
            Text& add(const char* t) noexcept
            {
                const std::size_t k = t != nullptr ? std::strlen(t) : 0;
                if (n + k < N)
                {
                    std::memcpy(s + n, t, k);
                    n += k;
                    s[n] = '\0';
                }
                return *this;
            }
            void set(const char* t) noexcept
            {
                n = 0;
                s[0] = '\0';
                add(t);
            }
        };

        // v with dp decimals ("−14.2", U+2212); `sign` adds "+" to a value that prints above zero. Floor values: "–".
        template <std::size_t N>
        void addNumber(Text<N>& t, float v, int dp, bool sign) noexcept
        {
            if (std::isfinite(v) && v <= kFloorDb)
            {
                t.add(kDash);
                return;
            }
            char b[32];
            if (funkgui::fmt::db(v, dp, b, sizeof b) < 0)
            {
                t.add(kDash);
                return;
            }
            // fmt rounds half away from zero at dp decimals, as std::round does: "+" only when the printed value is > 0.
            if (sign && std::isfinite(v) && std::round(static_cast<double>(v) * std::pow(10.0, dp)) > 0.0)
                t.add("+");
            t.add(b);
        }

        // A ratio as the host prints it (01 §3.1): one decimal below 10, whole numbers from 10, ∞ beyond 1e4 ("3.2:1").
        template <std::size_t N>
        void addRatio(Text<N>& t, float r) noexcept
        {
            if (!std::isfinite(r) || std::fabs(r) >= kRatioInf)
            {
                t.add("\xE2\x88\x9E:1");                           // ∞:1
                return;
            }
            char b[32];
            if (funkgui::fmt::db(r, std::fabs(r) < 9.95f ? 1 : 0, b, sizeof b) < 0)
            {
                t.add(kDash);
                return;
            }
            t.add(b).add(":1");
        }

        // A time readout in the unit that keeps it readable (fmt::seconds: "250 µS", "2.5 MS", "1.2 S").
        template <std::size_t N>
        void addTime(Text<N>& t, float ms) noexcept
        {
            char b[32];
            const char* unit = "";
            if (!(ms > 0.0f) || !std::isfinite(ms) || funkgui::fmt::seconds(ms * 1.0e-3f, b, sizeof b, &unit) < 0)
            {
                t.add(kDash);
                return;
            }
            t.add(b).add(" ").add(unit);
        }

        const fcdsp::ModeDescriptor* descOf(const FrameState& f) noexcept
        {
            return f.entry != nullptr ? f.entry->desc : nullptr;
        }
    }

    // ---- the internals' text (shared with CONTROL PATH) -------------------------------------------------------------------

    int Readouts::internalText(const fcdsp::InternalSpec& spec, float value, char* out, std::size_t n) noexcept
    {
        if (out == nullptr || n == 0)
            return 0;
        const int dp = std::clamp(static_cast<int>(spec.decimals), 0, 6);
        const int len = funkgui::fmt::db(value, dp, out, n);
        if (len < 0)
        {
            out[0] = '\0';
            return 0;
        }
        auto used = static_cast<std::size_t>(len);
        const std::size_t u = spec.unit != nullptr ? std::strlen(spec.unit) : 0;
        if (u > 0 && std::isfinite(value))
        {
            if (used + 1 + u + 1 > n)
            {
                out[0] = '\0';
                return 0;
            }
            out[used++] = ' ';
            std::memcpy(out + used, spec.unit, u);
            used += u;
            out[used] = '\0';
        }
        return static_cast<int>(used);
    }

    // ---- state ------------------------------------------------------------------------------------------------------------

    struct Readouts::State
    {
        struct Line
        {
            Text<32> name;                                        // as drawn (cut with an ellipsis only when it must be)
            Text<40> value;                                       // as drawn
            const char* full = "";                                // the uncut name (the a11y title)
            const char* meaning = "";                             // the a11y description
            bool shown = false;                                   // rows 11–18: only the declared internals
            bool live = false;                                    // a live value (ink100) or "–" (ink16)
        };
        std::array<Line, kRows> rows{};
        const char* pairs = nullptr;                              // "L/R" | "M/S" while any row shows a pair
        bool separator = false;                                   // the Mode declares internals
    };

    Readouts::Readouts(PanelContext& ctx, const layout::ReadoutsGeom& geom, uint32_t idBase)
        : ctx_(ctx), geom_(geom), idBase_(idBase), st_(std::make_unique<State>())
    {
        tick(0.0f);                                              // the rows' names exist before the first frame
    }

    Readouts::~Readouts() = default;

    // ---- tick: every row's text from this frame ----------------------------------------------------------------------------

    void Readouts::tick(float)
    {
        State& s = *st_;
        const FrameState& f = ctx_.frame;
        const fcdsp::ModeDescriptor* d = descOf(f);
        const fcdsp::UiFrame& u = f.ui;
        const bool live = f.live && d != nullptr && static_cast<int>(u.modeSlot) == static_cast<int>(f.res.view.slot);
        const int rows = std::min(geom_.rows, kRows);

        // Names and which rows exist.
        for (int i = 0; i < kRows; ++i)
        {
            State::Line& l = s.rows[static_cast<std::size_t>(i)];
            l.shown = false;
            l.live = false;
            l.name.set("");
            l.value.set("");
            l.full = "";
            l.meaning = "";
        }
        for (int i = 0; i < kFixedRows && i < rows; ++i)
        {
            State::Line& l = s.rows[static_cast<std::size_t>(i)];
            l.shown = true;
            l.name.set(R::kNames[static_cast<std::size_t>(i)]);
            l.full = R::kNames[static_cast<std::size_t>(i)];
            l.meaning = kMeaning[static_cast<std::size_t>(i)];
        }
        const std::size_t nInt = d != nullptr ? std::min<std::size_t>(d->internals.size(), kMaxInternals) : 0;
        for (std::size_t i = 0; i < nInt && R::kFirstInternalRow + static_cast<int>(i) < rows; ++i)
        {
            State::Line& l = s.rows[static_cast<std::size_t>(R::kFirstInternalRow) + i];
            const fcdsp::InternalSpec& in = d->internals[i];
            l.shown = true;
            l.name.set(in.name);
            l.full = in.name;
            l.meaning = "Mode internal";
        }
        s.separator = nInt > 0;
        s.pairs = nullptr;

        if (!live)
        {
            for (State::Line& l : s.rows)
                if (l.shown)
                    l.value.set(kDash);
        }
        else
        {
            // The lane with the larger applied GR (the operating dot's), and the dot's 10 ms peak envelope (TransferPlot).
            const int lane = u.appliedGrDb[1] > u.appliedGrDb[0] ? 1 : 0;
            const auto ul = static_cast<std::size_t>(lane);
            float cx = u.curveXDb[ul], tgt = u.targetGrDb[ul];
            {
                const HistoryStore& h = ctx_.history;
                const uint64_t count = h.count();
                const uint64_t e0 = std::max(count >= kEnvelopeMs ? count - kEnvelopeMs : 0, h.oldest());
                for (uint64_t e = e0; e < count; ++e)
                    if (const fcdsp::HistoryColumn& c = h.at(e); !HistoryStore::isGap(c))
                    {
                        cx = std::max(cx, c.detMaxDb);
                        tgt = std::max(tgt, c.tgtMaxDb);
                    }
            }
            const bool unlinked = f.eng.link < 1.0f;
            const bool detPair = unlinked && std::fabs(u.curveXDb[0] - u.curveXDb[1]) > R::kPairDb;
            const bool grPair = unlinked && std::fabs(u.appliedGrDb[0] - u.appliedGrDb[1]) > R::kPairDb;
            if (detPair || grPair)
                s.pairs = (u.flags & fcdsp::kUiMidSide) != 0 ? "M/S" : "L/R";

            const auto value = [&](int r) -> Text<40>& {
                State::Line& l = s.rows[static_cast<std::size_t>(r)];
                l.live = true;
                return l.value;
            };
            if (detPair)
            {
                addNumber(value(rDet), u.curveXDb[0], 1, false);
                addNumber(value(rDet).add("/"), u.curveXDb[1], 1, false);
            }
            else
            {
                addNumber(value(rDet), cx, 1, false);
            }
            if (cx > kFloorDb)
                addNumber(value(rOver), cx - fcdsp::analysis::inputThresholdDb(f.eng), 1, true);
            else
                value(rOver).add(kDash);
            addNumber(value(rTarget), std::max(tgt, 0.0f), 1, false);
            if (grPair)
            {
                addNumber(value(rApplied), u.appliedGrDb[0], 1, false);
                addNumber(value(rApplied).add("/"), u.appliedGrDb[1], 1, false);
            }
            else
            {
                addNumber(value(rApplied), u.appliedGrDb[ul], 1, false);
            }
            if (d->stage2 == fcdsp::Stage2Kind::none)
                s.rows[static_cast<std::size_t>(rS2)].value.set(kDash);                     // no second stage: n/a, not a live zero
            else
                addNumber(value(rS2), u.s2GrDb[ul], 1, false);
            if (cx > kFloorDb)
                addRatio(value(rRatio), fcdsp::analysis::localRatio(*f.entry, f.eng, cx));
            else
                value(rRatio).add(kDash);
            const auto lawOf = [&](fcdsp::Pid p) {
                const fcdsp::ParamSpec* sp = f.res.view.spec[fcdsp::idx(p)];
                return fcdsp::lawFactor(sp != nullptr ? sp->law : fcdsp::TimeLaw::expDb);
            };
            addTime(value(rAtk), u.attackNowMs[ul] * lawOf(fcdsp::Pid::atk));
            addTime(value(rRel), u.releaseNowMs[ul] * lawOf(fcdsp::Pid::rel));
            addNumber(value(rCrest), u.crestDb[ul], 1, false);
            value(rPhase).add(kPhases[(u.flags >> (16u + 2u * static_cast<uint32_t>(lane))) & 3u]);
            for (std::size_t i = 0; i < nInt; ++i)
            {
                State::Line& l = s.rows[static_cast<std::size_t>(R::kFirstInternalRow) + i];
                if (!l.shown)
                    continue;
                char b[40];
                l.live = true;
                l.value.set(internalText(d->internals[i], u.internals[i], b, sizeof b) > 0 ? b : kDash);
            }
        }

        // A name never meets its value: cut the name (never the number) to the room the value leaves.
        for (State::Line& l : s.rows)
        {
            if (!l.shown)
                continue;
            const float room = geom_.valueRight - geom_.nameX - kNameGap
                             - funkgui::text::width(ctx_.atlas, l.value.s, T::kMicro);
            if (funkgui::text::fits(ctx_.atlas, l.name.s, T::kMicro, room))
                continue;
            char cut[sizeof l.name.s];
            if (funkgui::text::fitEllipsis(ctx_.atlas, l.name.s, T::kMicro, std::max(room, 0.0f), cut, sizeof cut) < 0)
                cut[0] = '\0';
            l.name.set(cut);
        }
    }

    // ---- draw -------------------------------------------------------------------------------------------------------------

    void Readouts::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const State& s = *st_;
        {
            const funkgui::Canvas::Scope scope(c, tag::caption, false);
            c.text("READOUTS", geom_.caption.x, geom_.caption.y, T::kCaption, th.ink52);
        }
        if (s.pairs != nullptr)
        {
            // "READOUTS · L/R": the order of every pair, named once (live).
            const funkgui::Canvas::Scope scope(c, tag::readoutName, true);
            Text<16> t;
            t.add(kMiddot).add(s.pairs);
            c.text(t.s, geom_.caption.x + c.textWidth("READOUTS", T::kCaption), geom_.caption.y, T::kCaption, th.ink32);
        }
        if (s.separator)
        {
            const funkgui::Canvas::Scope scope(c, tag::plotFrame, false);
            c.hairlineH(geom_.area.x, geom_.area.y + geom_.rowPitch * static_cast<float>(R::kFirstInternalRow),
                        geom_.area.w, th.ink16);
        }
        for (int i = 0; i < kRows && i < geom_.rows; ++i)
        {
            const State::Line& l = s.rows[static_cast<std::size_t>(i)];
            if (!l.shown)
                continue;
            const float centre = geom_.area.y + geom_.rowPitch * (static_cast<float>(i) + 0.5f);
            const float top = c.capCentreTop(centre, T::kMicro);
            {
                const funkgui::Canvas::Scope scope(c, tag::readoutName, false);
                c.text(l.name.s, geom_.nameX, top, T::kMicro, th.ink52);
            }
            const funkgui::Canvas::Scope scope(c, tag::readoutValue, true);
            c.text(l.value.s, geom_.valueRight, top, T::kMicro, l.live ? th.ink100 : th.ink16, funkgui::Align::right);
        }
    }

    bool Readouts::hit(funkgui::Point p) const { return geom_.area.contains(p); }

    // ---- accessibility ----------------------------------------------------------------------------------------------------

    void Readouts::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const State& s = *st_;
        funkgui::A11yItem it;
        it.id = idBase_ + kImageId;
        it.role = funkgui::A11yRole::image;
        it.bounds = geom_.area;
        it.title = s.pairs != nullptr ? std::string("Readouts, pairs ") + s.pairs : std::string("Readouts");
        it.readOnly = true;
        out.push_back(std::move(it));
        for (int i = 0; i < kRows && i < geom_.rows; ++i)
        {
            const State::Line& l = s.rows[static_cast<std::size_t>(i)];
            if (!l.shown)
                continue;
            funkgui::A11yItem r;
            r.id = idBase_ + kFirstRowId + static_cast<uint32_t>(i);
            r.parent = idBase_ + kImageId;
            r.role = funkgui::A11yRole::staticText;
            r.bounds = { geom_.area.x, geom_.area.y + geom_.rowPitch * static_cast<float>(i), geom_.area.w,
                         geom_.rowPitch };
            r.title = l.full;
            r.value = l.value.s;
            r.description = l.meaning;
            r.readOnly = true;
            out.push_back(std::move(r));
        }
    }

    int Readouts::focusOrder(std::span<uint32_t>) const { return 0; }
}
