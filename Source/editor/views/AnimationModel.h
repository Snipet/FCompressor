// Source/editor/views/AnimationModel.h — the ANIMATION preference as a stepped slot (v1.2, ADR-90): how fast the UI's
// fades and eases run, for every FCompressor on this computer. The settings screen draws it with a RuleSlider.
//
// - Five detents, slow to fast: SLOW (×2 the time), NORMAL (×1, the default), FAST (×0.5), FASTER (×0.25), OFF (no
//   animation). The value is funkgui::ease's process-wide time scale (FunkGui v0.10.0), which multiplies every ease's
//   tau; the product's own timed fades (the Mode colour, the live marks' fade, the landing flash, a typed field's
//   flash) use scaledStep() below, so they follow it too. Meters, clocks, dwells and messages (SAVED, COPIED, the
//   first-run hint) keep their own time: they are not animations.
// - Stored in UiPreferences under layout::settings::kPrefAnimation as the detent index 0 … 4; a missing or damaged value
//   is NORMAL. apply() sets the scale from it (the Panel calls it when it is built), and a write sets it at once, so
//   every open editor in the process follows.
// - The model is its own ParamPort, so RuleSlider writes it exactly as it writes a parameter (a click on a label, a
//   drag, the wheel, the keys, a11y): begin and end are nothing, setValue01 writes the preference. There is no host
//   parameter and no host menu (native() is nullptr).
#pragma once

#include <funkgui/params/ParamPort.h>
#include <funkgui/widgets/ValueModel.h>

#include <array>
#include <cstdint>

namespace fcmp::ui
{
    class AnimationModel final : public funkgui::ValueModel, private funkgui::ParamPort
    {
    public:
        static constexpr int kSteps = 5;
        static constexpr int kDefaultIndex = 1;                  // NORMAL
        static constexpr std::array<float, kSteps> kScales { 2.0f, 1.0f, 0.5f, 0.25f, 0.0f };

        static int   index() noexcept;                           // the preference, 0 … 4
        static float scale(int index) noexcept;                  // kScales, NORMAL out of range
        static void  apply() noexcept;                           // funkgui::ease::setTimeScale(scale(index()))
        // One step of a product fade that takes `seconds` at NORMAL: dt / (seconds × the scale), or 1 (land now) when
        // the scale is 0 (OFF) or seconds is not positive.
        static float scaledStep(float dt, float seconds) noexcept;

        AnimationModel();                                        // fills the detent table (constant texts)

        // ValueModel
        uint64_t           key() const override;
        void               view(funkgui::ValueView&) const override;
        funkgui::ParamPort* port() override { return this; }
        float              host01FromTrack(float t) const override { return t; }
        float              defaultHost01() const override;

    private:
        // ParamPort (the preference)
        float       value01() const override;
        float       default01() const override;
        int         numSteps() const override { return kSteps; }
        void        beginGesture() override {}
        void        setValue01(float) override;
        void        endGesture() override {}
        const char* id() const override { return "animation"; }
        void*       native() const override { return nullptr; }

        std::array<funkgui::Detent, kSteps> detents_ {};
    };
}
