// Source/editor/views/EditControls.cpp — UNDO, REDO and A | B on the preset strip (see EditControls.h).
#include "editor/views/EditControls.h"

#include "editor/Layout.h"
#include "editor/ProductTheme.h"
#include "editor/SubView.h"
#include "editor/Tags.h"

#include "plugin/ProcessorFacade.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Ease.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/juce/MenuLook.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/widgets/FocusRing.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>

namespace fcmp::ui
{
    namespace
    {
        namespace E = layout::edits;
        namespace T = funkgui::type;
        using Part = EditControls::Part;

        constexpr float kArcR = 4.0f;                            // the arrows: an arc and a head
        constexpr float kHead = 2.5f;
        constexpr float kStroke = 1.25f;
        constexpr int   kArcSteps = 10;
        constexpr float kPi = 3.14159265f;

        constexpr const char* kCompareSpec = "A/B   COMPARE TWO SOUNDS   CLICK THE OTHER LETTER   RIGHT-CLICK: COPY";
        constexpr int kCopyA = 1, kCopyB = 2;                    // menu item ids: copy into A, into B

        const funkgui::Rect& rectOf(Part p) noexcept
        {
            switch (p)
            {
                case Part::undo: return E::kUndo;
                case Part::redo: return E::kRedo;
                case Part::a:    return E::kA;
                case Part::b:    return E::kB;
                case Part::none: break;
            }
            return E::kUndo;
        }

        std::size_t indexOf(Part p) noexcept { return static_cast<std::size_t>(p) - 1u; }

        // A hook arrow in the rect: an arc over the top, the head at the left end (undo) or the right end (redo).
        void arrow(funkgui::Canvas& c, const funkgui::Rect& r, bool left, funkgui::Col ink)
        {
            const float cx = r.centreX(), cy = E::kCentreY + 1.0f;
            std::array<float, kArcSteps + 1> xs{}, ys{};
            for (int i = 0; i <= kArcSteps; ++i)                // from the left end (π) over the top to the right (0)
            {
                const float a = kPi - kPi * static_cast<float>(i) / static_cast<float>(kArcSteps);
                xs[static_cast<std::size_t>(i)] = cx + kArcR * std::cos(a);
                ys[static_cast<std::size_t>(i)] = cy - kArcR * std::sin(a);
            }
            c.polyline(xs.data(), ys.data(), kArcSteps + 1, kStroke, ink);
            const float tx = left ? cx - kArcR : cx + kArcR, ty = cy;     // the arc's end the arrow points out of
            const float hx[3] = { tx - kHead, tx, tx + kHead };
            const float hy[3] = { ty - kHead, ty + 0.5f, ty - kHead };
            c.polyline(hx, hy, 3, kStroke, ink);
        }
    }

    EditAccess& EditControls::edits(PanelContext& ctx)
    {
        if (ctx.gestures != nullptr && ctx.gestures->wheeling() && !ctx.gestures->dragging())
            ctx.gestures->closeAll();                            // only the burst is open: it becomes an entry
        return ctx.facade.edits();
    }

    bool EditControls::commandOnly(const funkgui::Mods& m) noexcept
    {
       #if JUCE_MAC
        return m.cmd && !m.ctrl && !m.alt;
       #else
        return m.cmd && !m.alt;                                  // Ctrl is the command key: both flags are set
       #endif
    }

    const char* EditControls::commandKeyName() noexcept
    {
       #if JUCE_MAC
        return "CMD";
       #else
        return "CTRL";
       #endif
    }

    EditControls::EditControls(PanelContext& ctx) : ctx_(ctx) {}

    EditControls::~EditControls()
    {
        alive_.reset();
        juce::PopupMenu::dismissAllActiveMenus();                // a menu never outlives its look
    }

    bool EditControls::contains(funkgui::Point p) const noexcept { return partAt(p) != Part::none; }

    EditControls::Part EditControls::partAt(funkgui::Point p) const noexcept
    {
        for (const Part q : { Part::undo, Part::redo, Part::a, Part::b })
            if (rectOf(q).contains(p))
                return q;
        return Part::none;
    }

    bool EditControls::owns(uint32_t id) const noexcept
    {
        const uint32_t base = a11yId(ViewIndex::presetStrip, 0);
        return id >= base + kUndoLocal && id <= base + kGroupLocal + 2u;
    }

    EditControls::Part EditControls::focusedPart() const noexcept
    {
        if (!ctx_.focusVisible)
            return Part::none;
        const uint32_t f = ctx_.focus;
        if (f == a11yId(ViewIndex::presetStrip, kUndoLocal))
            return Part::undo;
        if (f == a11yId(ViewIndex::presetStrip, kRedoLocal))
            return Part::redo;
        if (f >= a11yId(ViewIndex::presetStrip, kGroupLocal) && f <= a11yId(ViewIndex::presetStrip, kGroupLocal + 2u))
            return slot_ == 0 ? Part::a : Part::b;
        return Part::none;
    }

    bool EditControls::settled() const noexcept
    {
        for (std::size_t i = 0; i < hoverAmt_.size(); ++i)
            if (!funkgui::ease::sameBits(hoverAmt_[i], hover_ != Part::none && indexOf(hover_) == i ? 1.0f : 0.0f))
                return false;
        return true;
    }

    // ---- tick and draw ----------------------------------------------------------------------------------------------

    void EditControls::tick(float dt, funkgui::Point pointer, bool pointerOver)
    {
        EditAccess& e = ctx_.facade.edits();
        const bool undo = e.canUndo(), redo = e.canRedo(), used = e.slotUsed(1);
        const int slot = e.compareSlot();
        if (undo != canUndo_ || redo != canRedo_ || slot != slot_ || used != usedB_)
            ++revision_;
        canUndo_ = undo;
        canRedo_ = redo;
        slot_ = slot;
        usedB_ = used;
        if (canUndo_)
            std::snprintf(undoSpec_, sizeof undoSpec_, "UNDO %s   %s-Z", e.undoName(), commandKeyName());
        else
            std::snprintf(undoSpec_, sizeof undoSpec_, "UNDO   NOTHING TO UNDO");
        if (canRedo_)
            std::snprintf(redoSpec_, sizeof redoSpec_, "REDO %s   SHIFT-%s-Z", e.redoName(), commandKeyName());
        else
            std::snprintf(redoSpec_, sizeof redoSpec_, "REDO   NOTHING TO REDO");

        hover_ = pointerOver ? partAt(pointer) : Part::none;
        for (const Part q : { Part::undo, Part::redo, Part::a, Part::b })
            hoverAmt_[indexOf(q)] = funkgui::ease::hover(hoverAmt_[indexOf(q)], hover_ == q, dt);

        if (hover_ != Part::none)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::hover, 0u, specOf(hover_));
        if (const Part f = focusedPart(); f != Part::none)
            ctx_.offerHand(fcdsp::kNoPid, HandKind::focus, ctx_.focus, specOf(f));
    }

    const char* EditControls::specOf(Part p) const noexcept
    {
        switch (p)
        {
            case Part::undo: return undoSpec_;
            case Part::redo: return redoSpec_;
            case Part::a: case Part::b: return kCompareSpec;
            case Part::none: break;
        }
        return "";
    }

    void EditControls::draw(funkgui::Canvas& c, const funkgui::Theme& th) const
    {
        const funkgui::Canvas::Scope scope(c, tag::editControls, false);
        const auto ink = [&](Part p, bool enabled) {
            if (!enabled)
                return th.ink16;
            if (pressed_ == p && armed_)
                return th.accent;
            return funkgui::mix(th.ink52, th.ink100, hoverAmt_[indexOf(p)]);
        };
        arrow(c, E::kUndo, true, ink(Part::undo, canUndo_));
        arrow(c, E::kRedo, false, ink(Part::redo, canRedo_));

        for (const Part p : { Part::a, Part::b })
        {
            const bool active = (p == Part::a ? 0 : 1) == slot_;
            const funkgui::Rect& r = rectOf(p);
            const funkgui::Col col = active ? th.ink100
                                   : pressed_ == p && armed_ ? th.accent
                                                             : funkgui::mix(th.ink32, th.ink70, hoverAmt_[indexOf(p)]);
            c.text(p == Part::a ? "A" : "B", r.centreX(), c.capCentreTop(E::kCentreY, T::kCaption), T::kCaption, col,
                   funkgui::Align::centre);
            if (active)
                c.hairlineH(r.centreX() - 4.0f, E::kCentreY + 6.0f, 8.0f, th.ink70);
        }
        if (const Part f = focusedPart(); f != Part::none)
        {
            const funkgui::Rect ring = f == Part::a || f == Part::b
                                           ? funkgui::Rect{ E::kA.x, E::kA.y, E::kB.right() - E::kA.x, E::kA.h }
                                           : rectOf(f);
            funkgui::drawFocusRing(c, ring, th.accent);
        }
    }

    // ---- input ------------------------------------------------------------------------------------------------------

    void EditControls::fire(Part p)
    {
        EditAccess& e = edits(ctx_);
        switch (p)
        {
            case Part::undo: e.undo(); break;
            case Part::redo: e.redo(); break;
            case Part::a:    e.selectSlot(0); break;
            case Part::b:    e.selectSlot(1); break;
            case Part::none: break;
        }
    }

    void EditControls::pointerDown(const funkgui::PointerEvent& e)
    {
        const Part p = partAt({ e.x, e.y });
        if (e.popup)
        {
            if (p == Part::a || p == Part::b)
                showMenu();
            return;
        }
        pressed_ = p;
        armed_ = p != Part::none;
    }

    void EditControls::pointerDrag(const funkgui::PointerEvent& e)
    {
        if (armed_ && partAt({ e.x, e.y }) != pressed_)
            armed_ = false;                                      // dragging off cancels
    }

    void EditControls::pointerUp(const funkgui::PointerEvent& e)
    {
        const Part p = pressed_;
        const bool fireIt = armed_ && partAt({ e.x, e.y }) == p;
        pressed_ = Part::none;
        armed_ = false;
        if (fireIt)
            fire(p);
    }

    bool EditControls::key(const funkgui::KeyEvent& e)
    {
        const Part f = focusedPart();
        if (f == Part::none)
            return false;
        const bool press = e.key == funkgui::Key::enter || e.key == funkgui::Key::space;
        if (f == Part::undo || f == Part::redo)
        {
            if (press)
                fire(f);
            return press;
        }
        EditAccess& ed = edits(ctx_);
        switch (e.key)
        {
            case funkgui::Key::left: case funkgui::Key::up: case funkgui::Key::home:     ed.selectSlot(0); return true;
            case funkgui::Key::right: case funkgui::Key::down: case funkgui::Key::end:   ed.selectSlot(1); return true;
            case funkgui::Key::enter: case funkgui::Key::space:                          ed.selectSlot(slot_ == 0 ? 1 : 0);
                                                                                         return true;
            case funkgui::Key::character: case funkgui::Key::tab: case funkgui::Key::pageUp:
            case funkgui::Key::pageDown: case funkgui::Key::escape: case funkgui::Key::backspace:
            case funkgui::Key::del:
                break;
        }
        return false;
    }

    funkgui::Cursor EditControls::cursor(funkgui::Point p) const
    {
        return contains(p) ? funkgui::Cursor::pointingHand : funkgui::Cursor::normal;
    }

    void EditControls::showMenu()
    {
        juce::Component* owner = ctx_.host != nullptr ? ctx_.host->ownerComponent() : nullptr;
        if (owner == nullptr)
            return;                                              // headless: no window to anchor a menu on
        if (menuLook_ == nullptr)
            menuLook_ = std::make_unique<funkgui::MenuLook>(productTheme(ctx_.host->themeIndex()));
        menuLook_->setTheme(productTheme(ctx_.host->themeIndex()));
        juce::PopupMenu m;
        m.setLookAndFeel(menuLook_.get());
        m.addItem(slot_ == 0 ? kCopyB : kCopyA, slot_ == 0 ? "Copy A to B" : "Copy B to A");
        const float s = static_cast<float>(owner->getWidth()) / static_cast<float>(layout::kWidth);   // the UI zoom
        const funkgui::Rect r { E::kA.x, E::kA.y, E::kB.right() - E::kA.x, E::kA.h };
        const juce::Rectangle<int> area =
            owner->localAreaToGlobal(juce::Rectangle<float>(r.x * s, r.y * s, r.w * s, r.h * s).toNearestInt());
        const std::weak_ptr<int> alive = alive_;
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(owner).withTargetScreenArea(area),
                        [this, alive](int id) {
                            if (alive.expired() || id <= 0)
                                return;
                            edits(ctx_).copySlot();              // the live sound into the other slot
                            if (ctx_.host != nullptr)
                                ctx_.host->nudgeFullRate();
                        });
    }

    // ---- accessibility ------------------------------------------------------------------------------------------------

    void EditControls::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        const auto button = [&](uint32_t local, const char* title, const funkgui::Rect& r, bool enabled,
                                const char* spec) {
            funkgui::A11yItem it;
            it.id = a11yId(ViewIndex::presetStrip, local);
            it.role = funkgui::A11yRole::button;
            it.bounds = r;
            it.title = title;
            it.enabled = enabled;
            it.help = spec;
            out.push_back(std::move(it));
        };
        button(kUndoLocal, "Undo", E::kUndo, canUndo_, undoSpec_);
        button(kRedoLocal, "Redo", E::kRedo, canRedo_, redoSpec_);

        funkgui::A11yItem g;
        g.id = a11yId(ViewIndex::presetStrip, kGroupLocal);
        g.role = funkgui::A11yRole::radioGroup;
        g.bounds = { E::kA.x, E::kA.y, E::kB.right() - E::kA.x, E::kA.h };
        g.title = "Compare";
        g.value = slot_ == 0 ? "A" : "B";
        g.help = "Two sounds to compare; its menu copies one into the other";
        out.push_back(std::move(g));
        for (int i = 0; i < 2; ++i)
        {
            funkgui::A11yItem r;
            r.id = a11yId(ViewIndex::presetStrip, kGroupLocal + 1u + static_cast<uint32_t>(i));
            r.parent = a11yId(ViewIndex::presetStrip, kGroupLocal);
            r.role = funkgui::A11yRole::radioButton;
            r.bounds = i == 0 ? E::kA : E::kB;
            r.title = i == 0 ? "A" : "B";
            r.checkable = true;
            r.checked = slot_ == i;
            out.push_back(std::move(r));
        }
    }

    int EditControls::focusOrder(std::span<uint32_t> out) const
    {
        const uint32_t ids[3] = { a11yId(ViewIndex::presetStrip, kUndoLocal), a11yId(ViewIndex::presetStrip, kRedoLocal),
                                  a11yId(ViewIndex::presetStrip, kGroupLocal) };
        std::size_t n = 0;
        for (const uint32_t id : ids)
            if (n < out.size())
                out[n++] = id;
        return static_cast<int>(n);
    }

    void EditControls::a11yAction(uint32_t id, funkgui::A11yAction a)
    {
        const bool press = a == funkgui::A11yAction::press || a == funkgui::A11yAction::toggle;
        if (id == a11yId(ViewIndex::presetStrip, kUndoLocal) && press)
            fire(Part::undo);
        else if (id == a11yId(ViewIndex::presetStrip, kRedoLocal) && press)
            fire(Part::redo);
        else if (id == a11yId(ViewIndex::presetStrip, kGroupLocal) && a == funkgui::A11yAction::showMenu)
            showMenu();
        else if (id == a11yId(ViewIndex::presetStrip, kGroupLocal + 1u) && press)
            fire(Part::a);
        else if (id == a11yId(ViewIndex::presetStrip, kGroupLocal + 2u) && press)
            fire(Part::b);
    }
}
