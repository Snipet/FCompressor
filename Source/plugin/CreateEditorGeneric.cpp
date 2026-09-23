// Source/plugin/CreateEditorGeneric.cpp: the editor factory of the headless plugin, of fcmp_probe_plugin, and of the
// GPU plugin until Source/plugin/CreateEditorGpu.cpp exists (U7; cmake/FcmpSources.cmake chooses, SPRINTS §7 D22).
// Scripts/release.sh refuses a build whose editor is this one.
#include "plugin/Processor.h"

namespace fcmp
{
    juce::AudioProcessorEditor* createEditor(Processor& processor)
    {
        return new juce::GenericAudioProcessorEditor(processor);
    }
} // namespace fcmp
