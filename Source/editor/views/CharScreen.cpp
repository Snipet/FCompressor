// Source/editor/views/CharScreen.cpp — the Characteristics screen's composition (see CharScreen.h): nine plots at
// their Layout.h geometries and the SC|COLOUR tab cells, every SubView call dispatched. U1a writes the composition
// complete; the plots are U2's, U3's and U4's. U3 (S9) adds the SC meter's "–" while no external key is active.
#include "editor/views/CharScreen.h"

#include "editor/Layout.h"
#include "editor/Panel.h"
#include "editor/Tags.h"
#include "editor/views/Telemetry.h"

#include "fcdsp/telemetry/UiFrame.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/canvas/Tags.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/widgets/FocusRing.h>

#include <array>
#include <cstddef>
#include <utility>

namespace fcmp::ui
{
    namespace
    {
        constexpr int kHistory = 0, kTransfer = 1, kSidechain = 7, kColour = 8;
        constexpr std::size_t kMaxStops = 256;
        constexpr const char* kScDash = "\xE2\x80\x93";            // U+2013: the SC meter without an external key
        constexpr float kScDashDy = 8.0f;                          // its centre, above the bar's floor

        void outline(funkgui::Canvas& c, const funkgui::Rect& r, funkgui::Col col)
        {
            c.hairlineH(r.x, r.y, r.w, col);
            c.hairlineH(r.x, r.bottom() - 1.0f, r.w, col);
            c.hairlineV(r.x, r.y, r.h, col);
            c.hairlineV(r.right() - 1.0f, r.y, r.h, col);
        }

        constexpr std::array<funkgui::Rect, 2> kTabs { layout::kTabSidechain, layout::kTabColour };
        constexpr std::array<const char*, 2> kTabLabels { "SIDECHAIN", "COLOUR" };

        funkgui::Rect tabGroupBounds() noexcept
        {
            return { kTabs[0].x, kTabs[0].y, kTabs[1].right() - kTabs[0].x, kTabs[0].h };
        }
    }

    CharScreen::CharScreen(PanelContext& ctx)
        : ctx_(ctx),
          history_(ctx, layout::kCharsHistory, plotIdBase(ViewIndex::charScreen, 0)),
          transfer_(ctx, layout::kCharsTransfer, plotIdBase(ViewIndex::charScreen, 1)),
          meters_(ctx, layout::kCharsMeters, plotIdBase(ViewIndex::charScreen, 2)),
          readouts_(ctx, layout::kReadouts, plotIdBase(ViewIndex::charScreen, 3)),
          controlPath_(ctx, layout::kControlPath, plotIdBase(ViewIndex::charScreen, 4)),
          attack_(ctx, layout::kStepAttack, plotIdBase(ViewIndex::charScreen, 5)),
          release_(ctx, layout::kStepRelease, plotIdBase(ViewIndex::charScreen, 6)),
          sidechain_(ctx, layout::kSidechain, plotIdBase(ViewIndex::charScreen, 7)),
          colour_(ctx, layout::kColour, plotIdBase(ViewIndex::charScreen, 8)),
          plots_{ &history_, &transfer_, &meters_, &readouts_, &controlPath_, &attack_, &release_, &sidechain_,
                  &colour_ }
    {
    }

    bool CharScreen::plotShown(int k) const noexcept
    {
        if (k == kSidechain)
            return ctx_.scTab == ScTab::sidechain;
        if (k == kColour)
            return ctx_.scTab == ScTab::colour;
        return k >= 0 && k < kPlots;
    }

    int CharScreen::plotAt(funkgui::Point p) const noexcept
    {
        for (int k = 0; k < kPlots; ++k)
            if (plotShown(k) && plots_[static_cast<std::size_t>(k)]->hit(p))
                return k;
        return -1;
    }

    int CharScreen::plotOf(uint32_t id) const noexcept
    {
        if (viewIndexOf(id) != static_cast<int>(ViewIndex::charScreen))
            return -1;
        const int k = plotIndexOf(id);
        return plotShown(k) ? k : -1;
    }

    int CharScreen::tabAt(funkgui::Point p) const noexcept
    {
        for (std::size_t i = 0; i < kTabs.size(); ++i)
            if (kTabs[i].contains(p))
                return static_cast<int>(i);
        return -1;
    }

    void CharScreen::pinTab(ScTab t)
    {
        if (t != ctx_.scTab)
            ctx_.panel.setView({ nullptr, ctx_.screen, t, ctx_.overlay }, false);
    }

    void CharScreen::tick(float dt)
    {
        for (int k = 0; k < kPlots; ++k)
            if (plotShown(k))
                plots_[static_cast<std::size_t>(k)]->tick(dt);
    }

    void CharScreen::keepTime(float dt)
    {
        history_.keepTime(dt);
        controlPath_.keepTime(dt);
    }

    void CharScreen::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        for (int k = 0; k < kPlots; ++k)
            if (plotShown(k))
                plots_[static_cast<std::size_t>(k)]->draw(c, th);
        // 02 §7.3 METERS: "SC = scPeakDb while kUiExtKeyActive, else –". MeterColumn draws the bar while the frame says
        // an external key is active; otherwise the bar stays empty and this names it n/a (ink16, at the bar's floor).
        const FrameState& f = ctx_.frame;
        if (!(telemetry::feed(ctx_) != telemetry::Feed::none && (f.ui.flags & fcdsp::kUiExtKeyActive) != 0))
            for (int i = 0; i < layout::kCharsMeters.nBars; ++i)
                if (const layout::MeterGeom::Bar& b = layout::kCharsMeters.bars[static_cast<std::size_t>(i)];
                    b.what == layout::MeterBar::sc)
                {
                    const funkgui::Canvas::Scope scope(c, tag::meterSc, true);
                    c.text(kScDash, b.r.centreX(), c.capCentreTop(b.r.bottom() - kScDashDy, funkgui::type::kMicro),
                           funkgui::type::kMicro, th.ink16, funkgui::Align::centre);
                }
        // The SC|COLOUR tab cells (02 §7.3: kCaption, text style, tab-pinned): active ink70, rest ink32.
        const funkgui::Canvas::Scope scope(c, tag::tab, false);
        for (std::size_t i = 0; i < kTabs.size(); ++i)
        {
            const bool active = static_cast<int>(ctx_.scTab) == static_cast<int>(i);
            const funkgui::Rect& r = kTabs[i];
            outline(c, r, th.ink16);
            c.text(kTabLabels[i], r.centreX(), c.capCentreTop(r.centreY(), funkgui::type::kCaption),
                   funkgui::type::kCaption, active ? th.ink70 : th.ink32, funkgui::Align::centre);
        }
        if (ctx_.focusVisible && ctx_.focus == a11yId(ViewIndex::charScreen, kTabGroupId))
            funkgui::drawFocusRing(c, tabGroupBounds(), th.accent);
    }

    bool CharScreen::hit(funkgui::Point p) const { return layout::kCharScreen.contains(p); }

    void CharScreen::pointerDown(const funkgui::PointerEvent& e)
    {
        const funkgui::Point p { e.x, e.y };
        if (const int t = tabAt(p); t >= 0 && !e.popup)
        {
            captured_ = kPlots;
            pinTab(t == 0 ? ScTab::sidechain : ScTab::colour);   // selects on down (HR :1709)
            return;
        }
        captured_ = plotAt(p);
        if (captured_ >= 0)
            plots_[static_cast<std::size_t>(captured_)]->pointerDown(e);
    }

    void CharScreen::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (captured_ >= 0 && captured_ < kPlots)
            plots_[static_cast<std::size_t>(captured_)]->pointerDrag(e);
    }

    void CharScreen::pointerUp(const funkgui::PointerEvent& e)
    {
        if (captured_ >= 0 && captured_ < kPlots)
            plots_[static_cast<std::size_t>(captured_)]->pointerUp(e);
        captured_ = -1;
    }

    void CharScreen::doubleClick(const funkgui::PointerEvent& e)
    {
        const int k = captured_ >= 0 && captured_ < kPlots ? captured_ : plotAt({ e.x, e.y });
        if (k >= 0)
            plots_[static_cast<std::size_t>(k)]->doubleClick(e);
    }

    bool CharScreen::wheel(const funkgui::WheelEvent& e)
    {
        const int k = plotAt({ e.x, e.y });
        return k >= 0 && plots_[static_cast<std::size_t>(k)]->wheel(e);
    }

    bool CharScreen::key(const funkgui::KeyEvent& e)
    {
        if (ctx_.focus == a11yId(ViewIndex::charScreen, kTabGroupId))
        {
            // 02 §8.9's cell-group keys (S13 H1a: ↑ ↓ Return Space were not taken, so a host saw them too): ↑ → next,
            // ↓ ← previous, Home / End first / last; Return and Space select the focused cell, which is the pinned one.
            switch (e.key)
            {
                case funkgui::Key::left:
                case funkgui::Key::down:
                case funkgui::Key::home:  pinTab(ScTab::sidechain); return true;
                case funkgui::Key::right:
                case funkgui::Key::up:
                case funkgui::Key::end:   pinTab(ScTab::colour);    return true;
                case funkgui::Key::enter:
                case funkgui::Key::space: return true;
                case funkgui::Key::character: case funkgui::Key::tab:     case funkgui::Key::pageUp:
                case funkgui::Key::pageDown:  case funkgui::Key::escape:  case funkgui::Key::backspace:
                case funkgui::Key::del:
                    return false;
            }
            return false;
        }
        const int k = plotOf(ctx_.focus);
        return k >= 0 && plots_[static_cast<std::size_t>(k)]->key(e);
    }

    void CharScreen::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const uint32_t group = a11yId(ViewIndex::charScreen, kTabGroupId);
        funkgui::A11yItem g;
        g.id = group;
        g.role = funkgui::A11yRole::radioGroup;
        g.bounds = tabGroupBounds();
        g.title = "Side chain or colour";
        g.value = ctx_.scTab == ScTab::sidechain ? "Side chain" : "Colour";
        out.push_back(std::move(g));
        for (std::size_t i = 0; i < kTabs.size(); ++i)
        {
            funkgui::A11yItem b;
            b.id = a11yId(ViewIndex::charScreen, i == 0 ? kTabSidechainId : kTabColourId);
            b.parent = group;
            b.role = funkgui::A11yRole::radioButton;
            b.bounds = kTabs[i];
            b.title = i == 0 ? "Side chain" : "Colour";
            b.checkable = true;
            b.checked = static_cast<int>(ctx_.scTab) == static_cast<int>(i);
            out.push_back(std::move(b));
        }
        for (int k = 0; k < kPlots; ++k)
        {
            const std::size_t first = out.size();
            plots_[static_cast<std::size_t>(k)]->accessibility(out);
            if (!plotShown(k))
                for (std::size_t i = first; i < out.size(); ++i)
                    out[i].visible = false;
        }
    }

    // 02 §7.5: the chrome stops of HISTORY and TRANSFER, the tab group, the other plots' chrome stops, then every
    // plot's handle stops in plot order.
    int CharScreen::focusOrder(std::span<uint32_t> out) const
    {
        std::array<uint32_t, kMaxStops> stops{};
        std::array<uint32_t, kMaxStops> handles{};
        std::size_t nStops = 0, nHandles = 0;
        const auto collect = [&](int k)
        {
            std::array<uint32_t, kMaxStops> tmp{};
            const int n = plots_[static_cast<std::size_t>(k)]->focusOrder(tmp);
            for (int i = 0; i < n && i < static_cast<int>(tmp.size()); ++i)
            {
                const uint32_t id = tmp[static_cast<std::size_t>(i)];
                if (isHandleId(id))
                {
                    if (nHandles < handles.size())
                        handles[nHandles++] = id;
                }
                else if (nStops < stops.size())
                {
                    stops[nStops++] = id;
                }
            }
        };
        collect(kHistory);
        collect(kTransfer);
        if (nStops < stops.size())
            stops[nStops++] = a11yId(ViewIndex::charScreen, kTabGroupId);
        for (int k = 0; k < kPlots; ++k)
            if (k != kHistory && k != kTransfer && plotShown(k))
                collect(k);
        std::size_t n = 0;
        for (std::size_t i = 0; i < nStops && n < out.size(); ++i)
            out[n++] = stops[i];
        for (std::size_t i = 0; i < nHandles && n < out.size(); ++i)
            out[n++] = handles[i];
        return static_cast<int>(n);
    }

    bool CharScreen::wantsFullRate() const
    {
        for (int k = 0; k < kPlots; ++k)
            if (plotShown(k) && plots_[static_cast<std::size_t>(k)]->wantsFullRate())
                return true;
        return false;
    }

    void CharScreen::pointerMove(const funkgui::PointerEvent& e)
    {
        const int k = plotAt({ e.x, e.y });
        if (k != hovered_ && hovered_ >= 0)
            plots_[static_cast<std::size_t>(hovered_)]->pointerExit();
        hovered_ = k;
        if (k >= 0)
            plots_[static_cast<std::size_t>(k)]->pointerMove(e);
    }

    void CharScreen::pointerExit()
    {
        if (hovered_ >= 0)
            plots_[static_cast<std::size_t>(hovered_)]->pointerExit();
        hovered_ = -1;
    }

    funkgui::Cursor CharScreen::cursor(funkgui::Point p) const
    {
        if (tabAt(p) >= 0)
            return funkgui::Cursor::pointingHand;
        const int k = captured_ >= 0 && captured_ < kPlots ? captured_ : plotAt(p);
        return k >= 0 ? plots_[static_cast<std::size_t>(k)]->cursor(p) : funkgui::Cursor::normal;
    }

    void CharScreen::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        const bool press = a == funkgui::A11yAction::press || a == funkgui::A11yAction::toggle;
        if (id == a11yId(ViewIndex::charScreen, kTabSidechainId))
        {
            if (press)
                pinTab(ScTab::sidechain);
            return;
        }
        if (id == a11yId(ViewIndex::charScreen, kTabColourId))
        {
            if (press)
                pinTab(ScTab::colour);
            return;
        }
        if (id == a11yId(ViewIndex::charScreen, kTabGroupId))
        {
            if (a == funkgui::A11yAction::increment)
                pinTab(ScTab::colour);
            else if (a == funkgui::A11yAction::decrement)
                pinTab(ScTab::sidechain);
            return;
        }
        const int k = plotOf(id);
        if (k >= 0)
            plots_[static_cast<std::size_t>(k)]->a11yAction(id, a, value);
    }

    uint32_t CharScreen::a11yRevision() const
    {
        uint32_t r = 0;
        for (const SubView* p : plots_)
            r += p->a11yRevision();
        return r;
    }
}
