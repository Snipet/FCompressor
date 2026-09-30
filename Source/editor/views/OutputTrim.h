// Source/editor/views/OutputTrim.h — the display row's OUTPUT trim (v1.2, ADR-88; layout::output): a compact slider over
// the `output` port (−24 … +24 dB), drawn and driven as a slot is, owned and routed by DisplayRow.
//
// - Text: fcdsp::formatOutputParts, the host's own text ("−3.0 DB"); the caret sits at the port's value01 on the rule.
// - Look (RuleSlider's): caption ink52 → ink70 and value ink100 → accent under the hand (ink32 at 0 dB), the hover fill
//   from the 0 dB notch to the caret in accentDim, the caret 7 px tall (11 under the hand) standing on the rule.
// - Pointer: a drag is one gesture, right or up increases, 240 px per full track (Shift 1200, Cmd 6000), re-anchored when
//   a modifier changes; a double-click writes 0 dB (inside the drag's gesture, which it re-anchors); a popup click opens
//   the host's parameter menu and never writes.
// - Wheel: RuleSlider's rates (0.025 of the track per wheel unit, Shift 0.005, a mouse notch x4, `reversed` honoured),
//   one gesture per burst.
// - Keys: arrows ±0.5 dB (Shift ±0.1), Page ±3 dB, Home −24, End +24, Delete/Backspace 0 dB; each is one tap.
// - A11y: a slider in dB (lo −24, hi +24, step 0.5), title "Output", value the spoken text; setValue takes dB.
#pragma once

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>

#include <cstdint>
#include <vector>

namespace funkgui
{
    class Canvas;
    class GestureController;
    class ParamPort;
    struct Theme;
}

namespace fcmp::ui
{
    struct PanelContext;

    class OutputTrim
    {
    public:
        OutputTrim(PanelContext&, uint32_t a11yId);

        OutputTrim(const OutputTrim&) = delete;
        OutputTrim& operator=(const OutputTrim&) = delete;

        uint32_t a11yId() const noexcept { return id_; }
        bool     contains(funkgui::Point) const noexcept;
        bool     dragging() const noexcept { return dragging_; }
        bool     settled() const noexcept;                       // no hover ease running, no drag
        const char* spec() const noexcept;                       // the footer line

        void tick(float dt, bool underHand);
        void draw(funkgui::Canvas&, const funkgui::Theme&, bool focusRing) const;

        void pointerDown(const funkgui::PointerEvent&, funkgui::GestureController&);
        void pointerDrag(const funkgui::PointerEvent&, funkgui::GestureController&);
        void pointerUp(funkgui::GestureController&);
        void reset(funkgui::GestureController&);                 // 0 dB: a double-click, Delete / Backspace
        bool wheel(const funkgui::WheelEvent&, funkgui::GestureController&, double nowSec);
        bool key(const funkgui::KeyEvent&, funkgui::GestureController&);

        void accessibility(std::vector<funkgui::A11yItem>&) const;
        void a11yAction(funkgui::A11yAction, double value, funkgui::GestureController&);

    private:
        float db() const;                                        // the port's plain value
        void  tapDb(float db, funkgui::GestureController&);      // one discrete write, legal()
        void  anchor(float x, float y, const funkgui::Mods&);    // the drag restarts here from the port's value

        PanelContext&       ctx_;
        funkgui::ParamPort& port_;
        uint32_t            id_;
        float               hover_ = 0.0f;                       // 0 … 1: 90 ms in, 160 ms out
        bool                dragging_ = false;
        float               anchorX_ = 0.0f, anchorY_ = 0.0f, anchor01_ = 0.0f;
        bool                fine_ = false, ultra_ = false;       // Shift / Cmd at the anchor
    };
}
