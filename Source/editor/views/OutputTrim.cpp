// Source/editor/views/OutputTrim.cpp — the display row's OUTPUT trim (see OutputTrim.h).
#include "editor/views/OutputTrim.h"

#include "editor/Layout.h"
#include "editor/SubView.h"
#include "editor/Tags.h"

#include "fcdsp/params/HostParams.h"
#include "fcdsp/params/Pid.h"
#include "fcdsp/params/Text.h"

#include <funkgui/canvas/Canvas.h>
#include <funkgui/core/Col.h>
#include <funkgui/core/Theme.h>
#include <funkgui/core/TypeScale.h>
#include <funkgui/panel/HostServices.h>
#include <funkgui/params/GestureController.h>
#include <funkgui/params/ParamPort.h>
#include <funkgui/widgets/FocusRing.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace fcmp::ui
{
    namespace
    {
        namespace O = layout::output;
        namespace T = funkgui::type;

        constexpr fcdsp::Pid kPid = fcdsp::Pid::output;
        constexpr float kHoverInS = 0.09f, kHoverOutS = 0.16f;   // RuleSlider's hover ease
        constexpr float kSpanFinePx = 1200.0f, kSpanUltraPx = 6000.0f;
        constexpr float kWheelStep = 0.025f, kWheelFine = 0.005f;   // track per wheel unit (RuleSlider)

        constexpr const char* kEntrySpec = "OUTPUT   TYPE A VALUE IN DB, \xE2\x88\x92" "24 TO 24   RETURN SETS IT   ESC CANCELS";
        constexpr const char* kSpec =
            "OUTPUT   THE LAST GAIN, AFTER THE MIX   \xC2\xB1" "24 DB   BYPASS AND SC LISTEN STAY UNTRIMMED";

        float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
        float lo() noexcept { return fcdsp::kHostParams[fcdsp::idx(kPid)].lo; }
        float hi() noexcept { return fcdsp::kHostParams[fcdsp::idx(kPid)].hi; }
    }

    OutputTrim::OutputTrim(PanelContext& ctx, uint32_t a11yId)
        : ctx_(ctx), port_(ctx.facade.port(kPid)), id_(a11yId)
    {
    }

    bool OutputTrim::contains(funkgui::Point p) const noexcept { return O::kHit.contains(p); }

    bool OutputTrim::settled() const noexcept
    {
        return !dragging_ && (hover_ <= 0.0f || hover_ >= 1.0f) && entry_.settled();
    }

    const char* OutputTrim::spec() const noexcept { return entry_.open() ? kEntrySpec : kSpec; }

    float OutputTrim::db() const { return fcdsp::toPlain(kPid, port_.value01()); }

    void OutputTrim::tapDb(float db, funkgui::GestureController& g)
    {
        if (!std::isfinite(db))
            return;
        g.tap(port_, fcdsp::toNorm(kPid, fcdsp::legal(kPid, db)));
    }

    void OutputTrim::anchor(float x, float y, const funkgui::Mods& m)
    {
        anchorX_ = x;
        anchorY_ = y;
        anchor01_ = port_.value01();
        fine_ = m.shift;
        ultra_ = m.cmd;
    }

    // ---- tick and draw --------------------------------------------------------------------------------------------------

    void OutputTrim::tick(float dt, bool underHand)
    {
        entry_.tick(dt);
        const float target = underHand || dragging_ ? 1.0f : 0.0f;
        const float step = std::max(dt, 0.0f) / (target > hover_ ? kHoverInS : kHoverOutS);
        hover_ = target > hover_ ? std::min(target, hover_ + step) : std::max(target, hover_ - step);
    }

    void OutputTrim::draw(funkgui::Canvas& c, const funkgui::Theme& th, bool focusRing) const
    {
        const float h = hover_;
        const float v01 = clamp01(port_.value01());
        const float x0 = O::kTrackX, w = O::kTrackW, ty = O::kTrackY;
        const float notchX = x0 + clamp01(fcdsp::toNorm(kPid, 0.0f)) * w;
        const float caretX = x0 + v01 * w;
        const bool atDefault = db() == 0.0f;
        const funkgui::Col valueCol = funkgui::mix(atDefault ? th.ink32 : th.ink100, th.accent, h);

        fcdsp::FormattedValue f;
        fcdsp::formatOutputParts(db(), f);
        char text[40];
        std::snprintf(text, sizeof text, "%s %s", f.value, f.unit);
        {
            const funkgui::Canvas::Scope scope(c, tag::outputTrim, false);
            c.text("OUTPUT", O::kCaption.x, O::kCaption.y, T::kCaption, funkgui::mix(th.ink52, th.ink70, h));
            if (h > 0.01f && caretX != notchX)                   // the hover fill, 0 dB -> the caret (RuleSlider)
            {
                const float a = std::min(notchX, caretX), b = std::max(notchX, caretX);
                c.rrect(a, ty - 1.0f, b - a, 3.0f, 0.0f, th.accentDim.withAlpha(h));
            }
            c.hairlineH(x0, ty, w, th.ink16);
            c.hairlineV(notchX, ty + 1.0f, 3.0f, th.ink32);      // the 0 dB notch
            const float caretH = 7.0f + 4.0f * h;
            c.rrect(caretX - 1.0f, ty + 3.5f - caretH, 2.0f, caretH, 0.0f, valueCol);
        }
        {
            const funkgui::Canvas::Scope scope(c, tag::outputValue, false);
            c.text(text, O::kValueRight, O::kCaption.y, T::kCaption, valueCol, funkgui::Align::right);
        }
        if (focusRing)
            funkgui::drawFocusRing(c, O::kHit, th.accent);
        entry_.draw(c, th);                                      // ADR-89: over the value
    }

    // ---- input ----------------------------------------------------------------------------------------------------------

    void OutputTrim::pointerDown(const funkgui::PointerEvent& e, funkgui::GestureController& g)
    {
        if (e.popup)
        {
            g.host().showParamMenu(port_, e.x, e.y);             // the host menu, never a write
            return;
        }
        g.beginDrag(port_);
        dragging_ = true;
        anchor(e.x, e.y, e.mods);
        ctx_.focus = id_;                                        // ADR-89: a number typed next goes here
    }

    void OutputTrim::pointerDrag(const funkgui::PointerEvent& e, funkgui::GestureController& g)
    {
        if (!dragging_)
            return;
        if (e.mods.shift != fine_ || e.mods.cmd != ultra_)
            anchor(e.x, e.y, e.mods);                            // a modifier changed: continue from here
        const float span = ultra_ ? kSpanUltraPx : (fine_ ? kSpanFinePx : O::kSpanPx);
        const float travel = (e.x - anchorX_) - (e.y - anchorY_); // right or up increases
        g.dragTo(clamp01(anchor01_ + travel / span));
    }

    void OutputTrim::pointerUp(funkgui::GestureController& g)
    {
        if (dragging_)
            g.endDrag();
        dragging_ = false;
    }

    void OutputTrim::reset(funkgui::GestureController& g)
    {
        tapDb(0.0f, g);                                          // inside the drag's gesture while one is open
        if (dragging_)
        {
            anchor01_ = port_.value01();                         // the drag continues from 0 dB
            anchorX_ = ctx_.pointer.x;
            anchorY_ = ctx_.pointer.y;
        }
    }

    bool OutputTrim::wheel(const funkgui::WheelEvent& e, funkgui::GestureController& g, double nowSec)
    {
        // macOS turns shift+scroll into a horizontal event: take whichever axis carries the motion (RuleSlider).
        const float delta = e.dy != 0.0f ? e.dy : e.dx;
        const float v = (e.reversed ? -1.0f : 1.0f) * delta;
        const float dv = v * (e.smooth ? 1.0f : 4.0f) * (e.mods.shift ? kWheelFine : kWheelStep);
        if (dv != 0.0f && std::isfinite(dv))
            g.wheelTo(port_, clamp01(port_.value01() + dv), nowSec);
        return true;
    }

    bool OutputTrim::key(const funkgui::KeyEvent& e, funkgui::GestureController& g)
    {
        if (entry_.open())                                       // ADR-89: the field takes every key
        {
            switch (entry_.key(e))
            {
                case ValueEntry::Result::commit: commitEntry(g); break;
                case ValueEntry::Result::cancel: closeEntry(); break;
                case ValueEntry::Result::typing: break;
            }
            return true;
        }
        if (ValueEntry::opens(e))
        {
            openEntry(e);
            return true;
        }
        const float step = e.mods.shift ? O::kKeyFineDb : O::kKeyDb;
        switch (e.key)
        {
            case funkgui::Key::up: case funkgui::Key::right:     tapDb(db() + step, g); return true;
            case funkgui::Key::down: case funkgui::Key::left:    tapDb(db() - step, g); return true;
            case funkgui::Key::pageUp:                           tapDb(db() + O::kPageDb, g); return true;
            case funkgui::Key::pageDown:                         tapDb(db() - O::kPageDb, g); return true;
            case funkgui::Key::home:                             tapDb(lo(), g); return true;
            case funkgui::Key::end:                              tapDb(hi(), g); return true;
            case funkgui::Key::del: case funkgui::Key::backspace: reset(g); return true;
            case funkgui::Key::character: case funkgui::Key::tab: case funkgui::Key::escape: case funkgui::Key::enter:
            case funkgui::Key::space:
                return false;
        }
        return false;
    }

    // ---- typed values (ADR-89) ----------------------------------------------------------------------------------------

    void OutputTrim::openEntry(const funkgui::KeyEvent& opener)
    {
        fcdsp::FormattedValue f;
        fcdsp::formatOutputParts(db(), f);
        const std::string current = std::string(f.value) + " " + f.unit;
        entry_.begin(O::kEntryBox, T::kLabel, opener, current);
        ctx_.textEntry = static_cast<int>(ViewIndex::displayRow);
        ctx_.textEntryBox = O::kEntryBox;
    }

    bool OutputTrim::commitEntry(funkgui::GestureController& g)
    {
        float v = 0.0f;
        if (!fcdsp::parseOutput(entry_.text(), v) || !std::isfinite(v))
        {
            entry_.refuse();
            return false;
        }
        tapDb(v, g);
        closeEntry();
        return true;
    }

    void OutputTrim::closeEntry() noexcept
    {
        if (entry_.open() && ctx_.textEntry == static_cast<int>(ViewIndex::displayRow))
            ctx_.textEntry = -1;
        entry_.end();
    }

    void OutputTrim::endEntry(bool commit, funkgui::GestureController& g)
    {
        if (!commit || !commitEntry(g))
            closeEntry();
    }

    // ---- accessibility --------------------------------------------------------------------------------------------------

    void OutputTrim::accessibility(std::vector<funkgui::A11yItem>& out) const
    {
        fcdsp::FormattedValue f;
        fcdsp::formatOutputParts(db(), f);
        funkgui::A11yItem it;
        it.id = id_;
        it.role = funkgui::A11yRole::slider;
        it.bounds = O::kHit;
        it.title = "Output";
        it.value = f.spoken;
        it.v = static_cast<double>(db());
        it.lo = static_cast<double>(lo());
        it.hi = static_cast<double>(hi());
        it.step = static_cast<double>(O::kKeyDb);
        out.push_back(std::move(it));
    }

    void OutputTrim::a11yAction(funkgui::A11yAction a, double value, funkgui::GestureController& g)
    {
        switch (a)
        {
            case funkgui::A11yAction::setValue:  tapDb(static_cast<float>(value), g); break;
            case funkgui::A11yAction::increment: tapDb(db() + O::kKeyDb, g); break;
            case funkgui::A11yAction::decrement: tapDb(db() - O::kKeyDb, g); break;
            case funkgui::A11yAction::showMenu:  g.host().showParamMenu(port_, O::kHit.centreX(), O::kHit.centreY()); break;
            case funkgui::A11yAction::press: case funkgui::A11yAction::toggle: case funkgui::A11yAction::focus:
                break;
        }
    }
}
