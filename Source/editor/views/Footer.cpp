// Source/editor/views/Footer.cpp — the footer (see Footer.h): the one-line hint / notice / summary / spec line, the ZOOM
// cells (UF1b) and the THEME cells.
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
#include <funkgui/panel/HostServices.h>
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
        constexpr uint32_t kZoomLocal  = 32;                     // the ZOOM radioGroup; its cells are 33 … 36

        constexpr const char* kFirstRun = "DRAG A VALUE OR THE CURVE. DOUBLE-CLICK TO RESET.";   // 02 §6.6
        constexpr const char* kPoison   = "AUDIO RESET AFTER A NON-FINITE SAMPLE";
        constexpr const char* kNewer    = "SESSION FROM A NEWER FCOMPRESSOR \xE2\x80\x94 LOADED BEST EFFORT";
        // UF1b: the preference specs fit the line's kSpecLineW (554 px; THEME's was "… FOR EVERY FCOMPRESSOR ON THIS
        // MACHINE, NOT SAVED WITH THE SESSION", 606 px): THEME 441 px, ZOOM 480 px.
        constexpr const char* kPrefNote  = "MACHINE-WIDE, NOT SAVED WITH THE SESSION";
        constexpr const char* kThemeSpec = "THEME   GRAPHITE \xC2\xB7 PAPER   MACHINE-WIDE, NOT SAVED WITH THE SESSION";
        constexpr const char* kSep = " \xC2\xB7 ";              // " · "
        constexpr funkgui::Point kNowhere { -1.0e6f, -1.0e6f };

        // UF1b: the ZOOM cells' texts, one per layout::footer::kZoomSteps (checked below).
        struct ZoomText
        {
            int         percent;
            const char* label;                                   // drawn
            const char* spoken;                                  // the radioButton's title
        };
        constexpr std::array<ZoomText, 4> kZoomText { { { 100, "100", "ZOOM 100 %" }, { 125, "125", "ZOOM 125 %" },
                                                        { 150, "150", "ZOOM 150 %" }, { 175, "175", "ZOOM 175 %" } } };
        constexpr bool zoomTextMatchesSteps() noexcept
        {
            for (std::size_t i = 0; i < kZoomText.size(); ++i)
                if (kZoomText[i].percent != F::kZoomSteps[i])
                    return false;
            return true;
        }
        static_assert(kZoomText.size() == F::kZoomSteps.size() && F::kZoomCells.size() == F::kZoomSteps.size()
                          && zoomTextMatchesSteps(),
                      "one ZOOM cell and text per zoom step");
        constexpr int kZoomCount = static_cast<int>(F::kZoomSteps.size());
        constexpr const char* kZoomTitle      = "Zoom";         // the radioGroup's spoken title (THEME's is "Theme")
        constexpr const char* kZoomWord       = "ZOOM";
        constexpr const char* kZoomUnavailable = "Needs a larger display";          // a11y help of an unavailable cell
        constexpr const char* kZoomNeeds      = " % NEEDS A LARGER DISPLAY";        // "150 % NEEDS A LARGER DISPLAY"
        constexpr const char* kZoomNeed       = " % NEED A LARGER DISPLAY";         // "150 · 175 % NEED A LARGER …"

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
                 a11yId(ViewIndex::footer, kThemeLocal)),
          zoom_(zoomCells_, std::vector<funkgui::Rect>(F::kZoomCells.begin(), F::kZoomCells.end()),
                funkgui::CellStyle::text, kZoomWord, F::kZoomCaption, a11yId(ViewIndex::footer, kZoomLocal))
    {
        if (ctx.options.skipHint)
            hint_.skip();                                        // probes and captures (02 §3.7 rule 6)
        movesSeen_ = ctx.pointerMoves;
        downsSeen_ = ctx.pointerDowns;
        modeSerial_ = ctx.frame.modeSerial;
        const StateNotice n = ctx.facade.stateNotice();          // a load before the editor opened: show it now
        rebuildNotice(n);
        zoom_.setSpokenTitle(kZoomTitle);
        readZoom();                                              // no host yet: 100 %, every step offered
    }

    // ---- the ZOOM cells (UF1b) ------------------------------------------------------------------------------------------

    int Footer::ZoomCells::count() const { return kZoomCount; }

    int Footer::ZoomCells::active() const { return footer_.zoomActive_; }

    const char* Footer::ZoomCells::label(int i) const
    {
        return i >= 0 && i < kZoomCount ? kZoomText[static_cast<std::size_t>(i)].label : "";
    }

    const char* Footer::ZoomCells::spoken(int i) const
    {
        return i >= 0 && i < kZoomCount ? kZoomText[static_cast<std::size_t>(i)].spoken : "";
    }

    bool Footer::ZoomCells::enabled(int i) const
    {
        return i >= 0 && i < kZoomCount && ((footer_.zoomFits_ >> static_cast<unsigned>(i)) & 1u) != 0;
    }

    const char* Footer::ZoomCells::help(int i) const { return enabled(i) ? nullptr : kZoomUnavailable; }

    void Footer::ZoomCells::select(int i, funkgui::GestureController&)
    {
        funkgui::HostServices* host = footer_.ctx_.host;
        if (host == nullptr || i < 0 || i >= kZoomCount || i == active() || !enabled(i))
            return;                                              // tap semantics; an unavailable step is refused
        host->setZoomPercent(F::kZoomSteps[static_cast<std::size_t>(i)]);   // the preference: no gesture, no batch
        footer_.readZoom();                                      // the host answers at once (HostServices.h)
    }

    void Footer::readZoom() noexcept
    {
        const funkgui::HostServices* host = ctx_.host;
        const int now = host != nullptr ? host->zoomPercent() : 100;
        const bool hasSteps = host != nullptr && !host->zoomSteps().empty();
        zoomActive_ = -1;
        zoomFits_ = 0;
        for (int i = 0; i < kZoomCount; ++i)
        {
            const int step = F::kZoomSteps[static_cast<std::size_t>(i)];
            if (step == now)
                zoomActive_ = i;
            if (!hasSteps || host->zoomFits(step))               // no steps: HostServices::zoomFits' default (true)
                zoomFits_ |= 1u << static_cast<unsigned>(i);
        }
    }

    bool Footer::themeFocused() const noexcept
    {
        const uint32_t id = theme_.selector().a11yId();
        return ctx_.focus >= id && ctx_.focus <= id + static_cast<uint32_t>(F::kThemeCells.size());
    }

    bool Footer::zoomFocused() const noexcept
    {
        const uint32_t id = zoom_.a11yId();
        return ctx_.focus >= id && ctx_.focus <= id + static_cast<uint32_t>(kZoomCount);
    }

    // "ZOOM   100 · 125 · 150 · 175 %   MACHINE-WIDE, NOT SAVED WITH THE SESSION" (as THEME's); with steps this display
    // cannot show, their note instead: "…   150 · 175 % NEED A LARGER DISPLAY" (one: "175 % NEEDS A LARGER DISPLAY").
    void Footer::zoomSpec(char* out, std::size_t cap) const
    {
        Line l{ out, cap };
        l.add(kZoomWord);
        l.add("   ");
        for (int i = 0; i < kZoomCount; ++i)
        {
            if (i > 0)
                l.add(kSep);
            l.add(kZoomText[static_cast<std::size_t>(i)].label);
        }
        l.add(" %   ");
        int unavailable = 0;
        for (int i = 0; i < kZoomCount; ++i)
            if (!zoomCells_.enabled(i))
            {
                if (unavailable++ > 0)
                    l.add(kSep);
                l.add(kZoomText[static_cast<std::size_t>(i)].label);
            }
        if (unavailable == 0)
            l.add(kPrefNote);
        else
            l.add(unavailable == 1 ? kZoomNeeds : kZoomNeed);
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

        const funkgui::Point pointer = pointerOver_ ? ctx_.pointer : kNowhere;
        theme_.selector().tick(dt, pointer);
        const uint32_t themeId = theme_.selector().a11yId();
        if (theme_.selector().contains(pointer))
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, themeId, kThemeSpec);
        if (ctx_.focusVisible && themeFocused())
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, themeId, kThemeSpec);

        // UF1b: the host's zoom as of this frame (a click in another editor, a display change), then the ZOOM cells.
        readZoom();
        zoom_.tick(dt, pointer);
        const uint32_t zoomId = zoom_.a11yId();
        if (const int cell = zoom_.cellAt(pointer); cell >= 0)
        {
            char spec[128];
            if (zoomCells_.enabled(cell))
                zoomSpec(spec, sizeof spec);
            else
            {
                Line l{ spec, sizeof spec };                     // ADR-68a: "150 % NEEDS A LARGER DISPLAY"
                l.add(kZoomText[static_cast<std::size_t>(cell)].label);
                l.add(kZoomNeeds);
            }
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, zoomId, spec);
        }
        if (ctx_.focusVisible && zoomFocused())
        {
            char spec[128];
            zoomSpec(spec, sizeof spec);
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, zoomId, spec);
        }
    }

    bool Footer::wantsFullRate() const
    {
        return hint_.wantsFullRate() || !theme_.selector().settled() || !zoom_.settled();
    }

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
            hint_.draw(c, th, F::kSpec.x, F::kSpec.y, F::kSpecLineW, nullptr);  // HINT, faded by the hint itself
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
            funkgui::text::fitEllipsis(ctx_.atlas, text, T::kLabel, F::kSpecLineW, fitted, sizeof fitted);
            c.text(fitted, F::kSpec.x, F::kSpec.y, T::kLabel, ink);
        }
        zoom_.draw(c, th, ctx_.focusVisible && zoomFocused());
        theme_.selector().draw(c, th, ctx_.focusVisible && themeFocused());
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    bool Footer::hit(funkgui::Point p) const { return layout::kFooter.contains(p); }

    void Footer::pointerMove(const funkgui::PointerEvent&) { pointerOver_ = true; }

    void Footer::pointerExit() { pointerOver_ = false; }

    void Footer::pointerDown(const funkgui::PointerEvent& e)
    {
        pointerOver_ = true;
        if (ctx_.gestures == nullptr)
            return;
        theme_.selector().pointerDown(e, *ctx_.gestures);       // preferences: no gesture, no parameter write
        readZoom();                                              // input may come before the first tick (UI_KEYS)
        zoom_.pointerDown(e, *ctx_.gestures);                   // (each takes only a press on its own cells)
    }

    bool Footer::key(const funkgui::KeyEvent& e)
    {
        if (ctx_.gestures == nullptr)
            return false;
        if (zoomFocused())
        {
            readZoom();                                          // the host's zoom now, even before the first tick
            return zoom_.key(e, *ctx_.gestures);
        }
        if (themeFocused())
            return theme_.selector().key(e, *ctx_.gestures);
        return false;
    }

    funkgui::Cursor Footer::cursor(funkgui::Point p) const
    {
        return zoom_.contains(p) ? zoom_.cursorAt(p) : theme_.selector().cursorAt(p);
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void Footer::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        char text[512];
        line(text, sizeof text);
        funkgui::A11yItem it;
        it.id = a11yId(ViewIndex::footer, kLineLocal);
        it.role = funkgui::A11yRole::staticText;
        it.bounds = { F::kSpec.x, layout::kFooter.y, F::kSpecLineW, layout::kFooter.h };
        it.title = "Footer";
        it.value = text;
        it.readOnly = true;
        out.push_back(std::move(it));
        zoom_.accessibility(out);                                // reading order: ZOOM, then THEME
        theme_.selector().accessibility(out);
    }

    int Footer::focusOrder(std::span<uint32_t> out) const
    {
        // ZOOM, then THEME: the panel's last Tab stop stays THEME (UF1b).
        const std::array<uint32_t, 2> stops { zoom_.a11yId(), theme_.selector().a11yId() };
        const std::size_t n = std::min(out.size(), stops.size());
        std::copy_n(stops.begin(), n, out.begin());
        return static_cast<int>(n);
    }

    void Footer::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        if (ctx_.gestures == nullptr)
            return;
        readZoom();
        if (!zoom_.a11yAction(id, a, value, *ctx_.gestures))     // false: not one of the ZOOM ids
            theme_.selector().a11yAction(id, a, value, *ctx_.gestures);
    }
}
