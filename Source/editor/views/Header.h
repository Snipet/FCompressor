// Source/editor/views/Header.h — the Header sub-view (02 §6.3, §8.5). Class declaration frozen at FZ4; U1b (S6) completes
// it: the wordmark, the topology caption (ModeDescriptor::topologyLine) and the Mode latch: caption
// MODE, ‹ and › stepping through the global order (Group, then slot) with one gesture per click or wheel
// burst on `mode` only (02 §8.4.3), the name cell opening the Mode browser (Panel::setView), and the group
// line "VCA · 2 OF 8". A11y: comboBox. Tab stop 1 of 02 §8.9.
//
// U1b (S6) behaviour, where 02 is silent (U1b handoff):
// - The topology caption is fitted into x 40…224 (the preset strip starts at 228): one kCaption line when it fits, else
//   two kCaption lines broken at a " · " separator (else at a space) so both fit, else the same in kMicro, else two
//   kMicro lines each fitted with an ellipsis.
// - "With dwell": the Mode-dependent texts (topology, name, group line) follow a DwellSelector<int> over the resolved
//   slot, so a Mode change from any source (chevrons, wheel, keys, the browser, the host) crossfades the old texts out
//   and the new ones in (τ 0.12 s, the ScreenFader's).
// - ‹ › step one Mode with one tap and wrap at the ends (the preset strip's ‹ › do); the wheel over ‹ name › is one
//   gesture per burst and stops at the ends, as do the arrow keys (Home / End: first / last). A popup click opens the
//   host menu of `mode`. The name arms on down and opens the browser on release inside (drag-off cancels); it is accent
//   only while armed. A hairline rule under the name (HR's preset-strip idiom) is ink16, ink32 under the pointer and
//   ink52 while the Mode browser is open.
// - Only `mode` is ever written, and only through the GestureController: a Mode change writes no other parameter (K2 #4).
//
// The members below the FZ4 declarations are additions (private state and SubView overrides with defaults); no FZ4
// declaration changed.
#pragma once

#include "editor/SubView.h"

#include "fcdsp/modes/Registry.h"

#include <funkgui/a11y/A11yItem.h>
#include <funkgui/core/Geometry.h>
#include <funkgui/panel/Input.h>
#include <funkgui/widgets/DwellSelector.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace fcmp::ui
{
    class Header final : public SubView
    {
    public:
        explicit Header(PanelContext&);

        Header(const Header&) = delete;
        Header& operator=(const Header&) = delete;

        void tick(float dt) override;
        void draw(funkgui::Canvas&, const funkgui::Theme&) const override;
        bool hit(funkgui::Point) const override;
        void accessibility(std::vector<funkgui::A11yItem>&) const override;
        int  focusOrder(std::span<uint32_t> out) const override;

        // ---- U1b additions: the SubView input the Mode latch takes -----------------------------------------------
        void pointerDown(const funkgui::PointerEvent&) override;
        void pointerDrag(const funkgui::PointerEvent&) override;
        void pointerUp(const funkgui::PointerEvent&) override;
        bool wheel(const funkgui::WheelEvent&) override;
        bool key(const funkgui::KeyEvent&) override;
        bool wantsFullRate() const override;
        void pointerMove(const funkgui::PointerEvent&) override;
        void pointerExit() override;
        funkgui::Cursor cursor(funkgui::Point) const override;
        void a11yAction(uint32_t id, funkgui::A11yAction, double value) override;

    private:
        enum class Part : uint8_t { none, prev, name, next };

        Part partAt(funkgui::Point) const noexcept;
        int  orderIndex() const noexcept;                        // the `mode` port's Mode in order_; -1: none
        void selectOrder(int index);                             // one tap of `mode` to order_[index]
        void stepMode(int delta, bool wrap);                     // chevrons (wrap) and keys (clamp)
        void openBrowser();
        void showMenu(float x, float y);
        void rebuildSpec();                                      // the latch's footer spec line for the shown Mode
        void drawModeTexts(funkgui::Canvas&, const funkgui::Theme&, int slot, float alpha) const;

        PanelContext& ctx_;
        std::array<uint8_t, fcdsp::kModeCapacity> order_{};     // registered slots in global order: Group, then slot
        int   nOrder_ = 0;
        funkgui::DwellSelector<int> shown_;                      // the slot whose texts are drawn (crossfaded)
        int   specSlot_ = -1;                                    // the slot spec_ was built for
        Part  hover_ = Part::none;                               // under the pointer (none while it is elsewhere)
        Part  pressed_ = Part::none;                             // between pointer down and up
        bool  armed_ = false;                                    // the name: released inside opens the browser
        bool  pointerOver_ = false;
        std::array<float, 3> hoverAmt_{};                        // prev, name, next (90 ms in, 160 ms out)
        float  wheelAcc_ = 0.0f;                                 // smooth wheel deltas not yet a whole notch
        double wheelLast_ = -1.0;                                // host time of the last wheel event (burst start)
        char   spec_[256]{};                                     // "MODE   <spec line>   …" (UTF-8)
        char   wordFirst_[8]{};                                  // the wordmark: "F" …
        char   wordRest_[32]{};                                  // … "COMPRESSOR" (from FcmpProduct.h)
    };
}
