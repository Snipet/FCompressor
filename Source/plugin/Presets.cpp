// Source/plugin/Presets.cpp: the processor's PresetAccess (02 §9.5; 01 §9.2). P1's body is EMPTY (no rows, nothing
// to apply or save) so the editor's preset strip and browser have a real object to draw from; P3 (S12) replaces this
// file with the FunkPresets-backed implementation (ProductConfig FCompressor / .fcmppreset / FCompressorPreset /
// FCMP_PRESETS_DB, hooks that bracket every apply with the facade's batch, the <PRESET> state hooks installed through
// PresetContext::stateHooks), without touching Processor.cpp (SPRINTS §7 D20).
#include "plugin/Processor.h"

#include <memory>

namespace fcmp
{
    namespace
    {
        class EmptyPresets final : public PresetAccess
        {
        public:
            int count() const override { return 0; }
            Row row(int index) const override
            {
                juce::ignoreUnused(index);
                return {};
            }
            int current() const override { return -1; }
            bool modified() const override { return false; }
            uint32_t revision() const override { return 0; }
            void apply(int index) override { juce::ignoreUnused(index); }
            void step(int delta) override { juce::ignoreUnused(delta); }
            bool saveAs(std::string_view name, std::string_view category) override
            {
                juce::ignoreUnused(name, category);
                return false;
            }
        };
    } // namespace

    std::unique_ptr<PresetAccess> makePresetAccess(const PresetContext& context)
    {
        juce::ignoreUnused(context);
        return std::make_unique<EmptyPresets>();
    }
} // namespace fcmp
