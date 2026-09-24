// Source/plugin/CreateEditorGpu.cpp: the editor factory of the GPU plugin (U7; 02 Part 2 intro; 03 §1.1). While this
// file exists, cmake/FcmpSources.cmake compiles it into the GPU configuration instead of CreateEditorGeneric.cpp and
// configure prints "FCompressor: GPU editor: Gpu" (SPRINTS §7 D22); the headless plugin and fcmp_probe_plugin keep the
// Generic editor. It is the one file under Source/plugin/ that includes editor/ (01 §2.2, lint plugin.editor).
//
// The editor's product hooks (FCMP_UI_VIEW, FCMP_UI_NO_HINT, FCMP_UI_NO_LIVE, FCMP_UI_FIXED_DT) are read here, once per
// editor, through funkgui::env() (02 §5.1); in a host none of them is set.
#include "plugin/Processor.h"

#include "editor/gpu/Editor.h"

namespace fcmp
{
    juce::AudioProcessorEditor* createEditor(Processor& processor)
    {
        return new ui::Editor(processor, processor, ui::EditorOptions::fromEnv());
    }
} // namespace fcmp
