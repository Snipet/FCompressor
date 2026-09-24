// Source/editor/views/Band.cpp — the band's composition (see Band.h): three plots at their Layout.h geometries, every
// SubView call dispatched to them. U1a writes the composition complete; the plots are U2's.
#include "editor/views/Band.h"

#include "editor/Layout.h"
#include "editor/Panel.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Theme.h>

#include <cstddef>

namespace fcmp::ui
{
    Band::Band(PanelContext& ctx)
        : ctx_(ctx),
          history_(ctx, layout::kBandHistory, plotIdBase(ViewIndex::band, 0)),
          transfer_(ctx, layout::kBandTransfer, plotIdBase(ViewIndex::band, 1)),
          meters_(ctx, layout::kBandMeters, plotIdBase(ViewIndex::band, 2)),
          plots_{ &history_, &transfer_, &meters_ }
    {
    }

    int Band::plotAt(funkgui::Point p) const noexcept
    {
        for (int k = 0; k < kPlots; ++k)
            if (plots_[static_cast<std::size_t>(k)]->hit(p))
                return k;
        return -1;
    }

    int Band::plotOf(uint32_t id) const noexcept
    {
        if (viewIndexOf(id) != static_cast<int>(ViewIndex::band))
            return -1;
        const int k = plotIndexOf(id);
        return k >= 0 && k < kPlots ? k : -1;
    }

    void Band::tick(float dt)
    {
        for (SubView* p : plots_)
            p->tick(dt);
    }

    void Band::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        for (const SubView* p : plots_)
            p->draw(c, th);
    }

    bool Band::hit(funkgui::Point p) const { return layout::kBand.contains(p); }

    void Band::pointerDown(const funkgui::PointerEvent& e)
    {
        captured_ = plotAt({ e.x, e.y });
        if (captured_ >= 0)
            plots_[static_cast<std::size_t>(captured_)]->pointerDown(e);
    }

    void Band::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (captured_ >= 0)
            plots_[static_cast<std::size_t>(captured_)]->pointerDrag(e);
    }

    void Band::pointerUp(const funkgui::PointerEvent& e)
    {
        if (captured_ >= 0)
            plots_[static_cast<std::size_t>(captured_)]->pointerUp(e);
        captured_ = -1;
    }

    // 02 §6.5: a double-click on empty band area opens CHARACTERISTICS. "Empty" is where no plot shows an interactive
    // cursor (a handle, the threshold line, a cell or a readout): there the plot gets the double-click instead.
    void Band::doubleClick(const funkgui::PointerEvent& e)
    {
        const funkgui::Point p { e.x, e.y };
        const int k = captured_ >= 0 ? captured_ : plotAt(p);
        if (k >= 0 && plots_[static_cast<std::size_t>(k)]->cursor(p) != funkgui::Cursor::normal)
        {
            plots_[static_cast<std::size_t>(k)]->doubleClick(e);
            return;
        }
        ctx_.panel.setView({ nullptr, Screen::characteristics, ctx_.scTab, ctx_.overlay }, false);
    }

    bool Band::wheel(const funkgui::WheelEvent& e)
    {
        const int k = plotAt({ e.x, e.y });
        return k >= 0 && plots_[static_cast<std::size_t>(k)]->wheel(e);
    }

    bool Band::key(const funkgui::KeyEvent& e)
    {
        const int k = plotOf(ctx_.focus);
        return k >= 0 && plots_[static_cast<std::size_t>(k)]->key(e);
    }

    void Band::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        for (const SubView* p : plots_)
            p->accessibility(out);
    }

    int Band::focusOrder(std::span<uint32_t> out) const
    {
        int n = 0;
        for (const SubView* p : plots_)
            n += p->focusOrder(out.subspan(static_cast<std::size_t>(n)));
        return n;
    }

    bool Band::wantsFullRate() const
    {
        for (const SubView* p : plots_)
            if (p->wantsFullRate())
                return true;
        return false;
    }

    void Band::pointerMove(const funkgui::PointerEvent& e)
    {
        const int k = plotAt({ e.x, e.y });
        if (k != hovered_ && hovered_ >= 0)
            plots_[static_cast<std::size_t>(hovered_)]->pointerExit();
        hovered_ = k;
        if (k >= 0)
            plots_[static_cast<std::size_t>(k)]->pointerMove(e);
    }

    void Band::pointerExit()
    {
        if (hovered_ >= 0)
            plots_[static_cast<std::size_t>(hovered_)]->pointerExit();
        hovered_ = -1;
    }

    funkgui::Cursor Band::cursor(funkgui::Point p) const
    {
        const int k = captured_ >= 0 ? captured_ : plotAt(p);
        return k >= 0 ? plots_[static_cast<std::size_t>(k)]->cursor(p) : funkgui::Cursor::normal;
    }

    void Band::a11yAction(uint32_t id, funkgui::A11yAction a, double value)
    {
        const int k = plotOf(id);
        if (k >= 0)
            plots_[static_cast<std::size_t>(k)]->a11yAction(id, a, value);
    }

    uint32_t Band::a11yRevision() const
    {
        uint32_t r = 0;
        for (const SubView* p : plots_)
            r += p->a11yRevision();
        return r;
    }
}
