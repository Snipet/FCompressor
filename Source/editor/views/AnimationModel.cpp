// Source/editor/views/AnimationModel.cpp — the ANIMATION preference (see AnimationModel.h).
#include "editor/views/AnimationModel.h"

#include "editor/Layout.h"

#include <funkgui/core/Ease.h>
#include <funkgui/prefs/UiPreferences.h>

#include <cmath>
#include <cstdio>

namespace fcmp::ui
{
    namespace
    {
        struct StepText
        {
            const char* label;                                   // detent label and value, upper case
            const char* spoken;
        };
        constexpr std::array<StepText, AnimationModel::kSteps> kTexts { {
            { "SLOW", "slow" }, { "NORMAL", "normal" }, { "FAST", "fast" }, { "FASTER", "faster" },
            { "OFF", "off, no animation" },
        } };

        float host01Of(int i) noexcept { return static_cast<float>(i) / static_cast<float>(AnimationModel::kSteps - 1); }
    }

    int AnimationModel::index() noexcept
    {
        return funkgui::UiPreferences::get().getInt(layout::settings::kPrefAnimation, kDefaultIndex, 0, kSteps - 1);
    }

    float AnimationModel::scale(int i) noexcept
    {
        return i >= 0 && i < kSteps ? kScales[static_cast<std::size_t>(i)] : kScales[kDefaultIndex];
    }

    void AnimationModel::apply() noexcept { funkgui::ease::setTimeScale(scale(index())); }

    float AnimationModel::scaledStep(float dt, float seconds) noexcept
    {
        const float t = seconds * funkgui::ease::timeScale();
        return t > 0.0f ? (dt > 0.0f ? dt / t : 0.0f) : 1.0f;
    }

    AnimationModel::AnimationModel()
    {
        for (int i = 0; i < kSteps; ++i)
            detents_[static_cast<std::size_t>(i)] = { host01Of(i), kTexts[static_cast<std::size_t>(i)].label,
                                                      kTexts[static_cast<std::size_t>(i)].spoken };
    }

    uint64_t AnimationModel::key() const { return static_cast<uint64_t>(index()) + 1u; }

    float AnimationModel::defaultHost01() const { return host01Of(kDefaultIndex); }

    float AnimationModel::value01() const { return host01Of(index()); }

    float AnimationModel::default01() const { return host01Of(kDefaultIndex); }

    void AnimationModel::setValue01(float v)
    {
        if (!(v >= 0.0f && v <= 1.0f))
            return;
        const auto i = static_cast<int>(std::lround(v * static_cast<float>(kSteps - 1)));
        funkgui::UiPreferences::get().setInt(layout::settings::kPrefAnimation, i);
        funkgui::ease::setTimeScale(scale(i));                   // every open editor follows at once
    }

    void AnimationModel::view(funkgui::ValueView& v) const
    {
        const int i = index();
        v = funkgui::ValueView{};
        v.state = funkgui::ValueState::stepped;
        v.label = "ANIMATION";
        v.track = host01Of(i);
        v.trackDefault = host01Of(kDefaultIndex);
        v.atDefault = i == kDefaultIndex;
        v.nDetents = kSteps;
        v.detent = i;
        v.detents = detents_.data();
        std::snprintf(v.text.value, sizeof v.text.value, "%s", kTexts[static_cast<std::size_t>(i)].label);
        std::snprintf(v.text.spoken, sizeof v.text.spoken, "%s", kTexts[static_cast<std::size_t>(i)].spoken);
    }
}
