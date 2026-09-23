// Source/editor/views/Footer.cpp — the footer (see Footer.h): the one-line hint / notice / summary / spec line and the
// THEME cells.
#include "editor/views/Footer.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/Tags.h"

#include "fcdsp/modes/ModeDescriptor.h"
#include "fcdsp/modes/Registry.h"
#include "fcdsp/params/ParamSpec.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Resolve.h"
#include "fcdsp/params/Setup.h"
#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/text/TextFit.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        namespace F = layout::footer;
        namespace T = funkgui::type;

        constexpr uint32_t kLineLocal  = 1;
        constexpr uint32_t kThemeLocal = 16;                     // the THEME radioGroup; its cells are 17, 18

        constexpr const char* kFirstRun = "DRAG A VALUE OR THE CURVE. DOUBLE-CLICK TO RESET.";   // 02 §6.6
        constexpr const char* kPoison   = "AUDIO RESET AFTER A NON-FINITE SAMPLE";
        constexpr const char* kNewer    = "SESSION FROM A NEWER FCOMPRESSOR \xE2\x80\x94 LOADED BEST EFFORT";
        constexpr const char* kThemeSpec =
            "THEME   GRAPHITE \xC2\xB7 PAPER   FOR EVERY FCOMPRESSOR ON THIS MACHINE, NOT SAVED WITH THE SESSION";
        constexpr const char* kSep = " \xC2\xB7 ";              // " · "
        constexpr funkgui::Point kNowhere { -1.0e6f, -1.0e6f };

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
            void add(int v) noexcept                             // a count (>= 0), without allocating
            {
                char digits[12];
                std::size_t i = sizeof digits;
                auto u = static_cast<unsigned>(v < 0 ? 0 : v);
                do
                {
                    digits[--i] = static_cast<char>('0' + u % 10u);
                    u /= 10u;
                } while (u != 0u && i > 0);
                add(std::string_view(digits + i, sizeof digits - i));
            }
            void addUpper(std::string_view s) noexcept           // a Mode key as the footer prints it ("OLD-KEY")
            {
                for (const char ch : s)
                {
                    const char u = ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
                    add(std::string_view(&u, 1));
                }
            }
        };

        std::string_view keyOf(const char (&k)[25]) noexcept     // StateNotice keys: NUL-terminated, <= 24 chars
        {
            std::size_t n = 0;
            while (n < sizeof k && k[n] != '\0')
                ++n;
            return { k, n };
        }

        // The name a notice prints for a key: the registered Mode's name, else the key in upper case.
        void addModeName(Line& line, std::string_view key) noexcept
        {
            if (const fcdsp::ModeEntry* e = fcdsp::byKey(key); e != nullptr && e->desc != nullptr)
                line.add(e->desc->name);
            else
                line.addUpper(key);
        }

        bool isRetired(std::string_view key) noexcept
        {
            for (const fcdsp::Retired& r : fcdsp::retired())
                if (r.key == key)
                    return true;
            return false;
        }
    }

    Footer::Footer(PanelContext& ctx)
        : ctx_(ctx),
          hint_(kFirstRun, F::kHintS),
          theme_(std::vector<funkgui::Rect>(F::kThemeCells.begin(), F::kThemeCells.end()), nullptr, funkgui::Point{},
                 a11yId(ViewIndex::footer, kThemeLocal))
    {
        if (ctx.options.skipHint)
            hint_.skip();                                        // probes and captures (02 §3.7 rule 6)
        movesSeen_ = ctx.pointerMoves;
        downsSeen_ = ctx.pointerDowns;
        modeSerial_ = ctx.frame.modeSerial;
        const StateNotice n = ctx.facade.stateNotice();          // a load before the editor opened: show it now
        rebuildNotice(n);
    }

    // ---- state ----------------------------------------------------------------------------------------------------------

    void Footer::rebuildNotice(const StateNotice& n)
    {
        noticeSerial_ = n.serial;
        Line line{ notice_, sizeof notice_ };
        const auto gap = [&line] {
            if (line.n > 0)
                line.add("   ");
        };
        if (n.newerSession)
            line.add(kNewer);
        if (n.modeMigrated)
        {
            gap();
            const std::string_view from = keyOf(n.fromKey);
            line.add("MODE '");
            line.addUpper(from);
            line.add(isRetired(from) ? "' IS RETIRED \xE2\x80\x94 LOADED '" : "' IS UNKNOWN \xE2\x80\x94 LOADED '");
            addModeName(line, keyOf(n.toKey));
            line.add("'");
        }
        if (n.modeRevised)
        {
            gap();
            const std::string_view to = keyOf(n.toKey);
            if (!to.empty())
                addModeName(line, to);
            else if (ctx_.frame.entry != nullptr && ctx_.frame.entry->desc != nullptr)
                line.add(ctx_.frame.entry->desc->name);
            else
                line.add("THE MODE");
            line.add(" UPDATED SINCE THIS SESSION (REV ");
            line.add(static_cast<int>(n.savedRev));
            line.add(" \xE2\x86\x92 ");                             // →
            line.add(static_cast<int>(n.currentRev));
            line.add(")");
        }
        noticeLeft_ = line.n > 0 ? F::kNoticeS : 0.0f;
    }

    void Footer::rebuildSummary()
    {
        // 02 §8.7: "BUS G · 6 STEPPED · 1 DERIVED · 6 EXTENSION · 8 N/A".
        const FrameState& f = ctx_.frame;
        Line line{ summary_, sizeof summary_ };
        if (f.entry == nullptr || f.entry->desc == nullptr)
            return;
        int stepped = 0, locked = 0, derived = 0, extension = 0, na = 0;
        for (std::size_t i = 0; i < fcdsp::kNumModeParams; ++i)
        {
            const fcdsp::ResolvedParam& r = f.res.view.p[i];
            const fcdsp::ParamSpec* s = f.res.view.spec[i];
            switch (r.state)
            {
                case fcdsp::SlotState::stepped: ++stepped; break;
                case fcdsp::SlotState::locked:  ++locked; break;
                case fcdsp::SlotState::derived: ++derived; break;
                case fcdsp::SlotState::na:      ++na; break;
                case fcdsp::SlotState::live:    break;
            }
            if (r.state != fcdsp::SlotState::na && s != nullptr && (s->flags & fcdsp::kFlagExtension) != 0)
                ++extension;
        }
        line.add(f.entry->desc->name);
        const std::array<std::pair<int, const char*>, 5> counts { { { stepped, " STEPPED" }, { locked, " LOCKED" },
                                                                    { derived, " DERIVED" },
                                                                    { extension, " EXTENSION" }, { na, " N/A" } } };
        for (const auto& [count, what] : counts)
            if (count > 0)
            {
                line.add(kSep);
                line.add(count);
                line.add(what);
            }
    }

    bool Footer::lookaheadHintWanted() const noexcept
    {
        const FrameState& f = ctx_.frame;
        if (f.entry == nullptr || f.entry->desc == nullptr || !f.entry->desc->wantsLookahead
            || f.raw.budget != fcdsp::LookaheadBudget::off)
            return false;
        const fcdsp::Pid p = ctx_.hand.pid;
        if (p == fcdsp::Pid::look || p == fcdsp::Pid::labudget)
            return true;
        if (!ctx_.pointerIn || ctx_.overlay != Overlay::none)
            return false;
        const funkgui::Rect& band = ctx_.screen == Screen::panel ? layout::kBand : layout::kCharScreen;
        return band.contains(ctx_.pointer);
    }

    void Footer::tick(float dt)
    {
        // The first-run hint: the first pointer move cuts it (HR :1668); a down with no move is always-chrome (HR :1488).
        if (ctx_.pointerMoves != movesSeen_)
        {
            movesSeen_ = ctx_.pointerMoves;
            hint_.pointerMoved();
        }
        if (ctx_.pointerDowns != downsSeen_)
        {
            downsSeen_ = ctx_.pointerDowns;
            hint_.noteDown();
        }
        hint_.tick(dt);

        const auto countDown = [dt](float& left) {
            if (dt > 0.0f)
                left = std::max(0.0f, left - dt);
        };
        countDown(noticeLeft_);
        countDown(poisonLeft_);
        countDown(summaryLeft_);

        if (const StateNotice n = ctx_.facade.stateNotice(); n.serial != noticeSerial_)
            rebuildNotice(n);                                    // a load while the editor is open

        const FrameState& f = ctx_.frame;
        if (f.fresh && (f.ui.flags & fcdsp::kUiPoisonReset) != 0 && f.ui.publishCount != poisonCount_)
        {
            poisonCount_ = f.ui.publishCount;                    // every new frame that carries it re-arms the 3 s
            poisonLeft_ = F::kPoisonS;
        }
        if (f.modeSerial != modeSerial_)
        {
            modeSerial_ = f.modeSerial;
            rebuildSummary();
            summaryLeft_ = summary_[0] != '\0' ? F::kSummaryS : 0.0f;
        }

        theme_.selector().tick(dt, pointerOver_ ? ctx_.pointer : kNowhere);
        const uint32_t themeId = theme_.selector().a11yId();
        if (pointerOver_ && theme_.selector().contains(ctx_.pointer))
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, themeId, kThemeSpec);
        if (ctx_.focusVisible && ctx_.focus >= themeId && ctx_.focus <= themeId + 2u)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, themeId, kThemeSpec);
    }

    bool Footer::wantsFullRate() const { return hint_.wantsFullRate() || !theme_.selector().settled(); }

    Footer::LineKind Footer::line(char* out, std::size_t cap) const
    {
        Line l{ out, cap };
        if (hint_.active())
        {
            l.add(kFirstRun);
            return LineKind::hint;
        }
        if (poisonLeft_ > 0.0f)
        {
            l.add(kPoison);
            return LineKind::poison;
        }
        if (noticeLeft_ > 0.0f && notice_[0] != '\0')
        {
            l.add(notice_);
            return LineKind::notice;
        }
        if (summaryLeft_ > 0.0f && summary_[0] != '\0')
        {
            l.add(summary_);
            return LineKind::summary;
        }
        if (lookaheadHintWanted())
        {
            // 02 §6.6: latency is never changed implicitly; the footer says what would fix it.
            l.add(ctx_.frame.entry->desc->name);
            l.add(" WITHOUT LOOKAHEAD CAN OVERSHOOT \xE2\x80\x94 SET LOOKAHEAD 5 MS ABOVE (+5 MS LATENCY)");
            return LineKind::lookahead;
        }
        if (ctx_.hand.spec[0] != '\0')
        {
            l.add(ctx_.hand.spec);
            return LineKind::spec;
        }
        return LineKind::none;
    }

    // ---- drawing --------------------------------------------------------------------------------------------------------

    void Footer::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        char text[512];
        const LineKind kind = line(text, sizeof text);
        if (kind == LineKind::hint)
            hint_.draw(c, th, F::kSpec.x, F::kSpec.y, F::kSpecMaxW, nullptr);   // HINT, faded by the hint itself
        else if (kind != LineKind::none)
        {
            const bool noticed = kind == LineKind::poison || kind == LineKind::notice || kind == LineKind::summary;
            const funkgui::Canvas::Scope scope(c, noticed ? tag::notice : tag::footerSpec, false);
            funkgui::Col ink = th.ink32;
            if (kind == LineKind::poison || kind == LineKind::notice)
                ink = th.ink70;
            else if (kind == LineKind::summary || kind == LineKind::lookahead)
                ink = th.ink52;
            char fitted[512];
            funkgui::text::fitEllipsis(ctx_.atlas, text, T::kLabel, F::kSpecMaxW, fitted, sizeof fitted);
            c.text(fitted, F::kSpec.x, F::kSpec.y, T::kLabel, ink);
        }
        const uint32_t themeId = theme_.selector().a11yId();
        theme_.selector().draw(c, th, ctx_.focusVisible && ctx_.focus >= themeId && ctx_.focus <= themeId + 2u);
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    bool Footer::hit(funkgui::Point p) const { return layout::kFooter.contains(p); }

    void Footer::pointerMove(const funkgui::PointerEvent&) { pointerOver_ = true; }

    void Footer::pointerExit() { pointerOver_ = false; }

    void Footer::pointerDown(const funkgui::PointerEvent& e)
    {
        pointerOver_ = true;
        if (ctx_.gestures != nullptr)
            theme_.selector().pointerDown(e, *ctx_.gestures);   // a preference: no gesture, no parameter write
    }

    bool Footer::key(const funkgui::KeyEvent& e)
    {
        const uint32_t themeId = theme_.selector().a11yId();
        if (ctx_.gestures == nullptr || ctx_.focus < themeId || ctx_.focus > themeId + 2u)
            return false;
        return theme_.selector().key(e, *ctx_.gestures);
    }

    funkgui::Cursor Footer::cursor(funkgui::Point p) const { return theme_.selector().cursorAt(p); }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void Footer::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        char text[512];
        line(text, sizeof text);
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::footer, kLineLocal);
        it.role = funkgui::A11yRole::staticText;
        it.bounds = { F::kSpec.x, layout::kFooter.y, F::kSpecMaxW, layout::kFooter.h };
        it.title = "Footer";
        it.value = text;
        it.readOnly = true;
        out.push_back(std::move(it));
        theme_.selector().accessibility(out);
    }

    int Footer::focusOrder(std::span<uint32_t> out) const
    {
        if (out.empty())
            return 0;
        out[0] = theme_.selector().a11yId();
        return 1;
    }

    void Footer::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        if (ctx_.gestures != nullptr)
            theme_.selector().a11yAction(id, a, value, *ctx_.gestures);
    }
}
