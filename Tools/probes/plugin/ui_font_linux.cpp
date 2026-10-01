// FCMP_PROBE layer=ui name=font_linux scope=global timeout=120 platform=linux
//
// ui.font_linux (ADR-92): ui.font (ui_font.cpp) on Linux. The same body, spec rows and golden row, under its own name
// so that its golden holds Linux's font.atlas.hash: FunkGui rasterises the glyphs with juce::Graphics into a native
// Image, which is CoreGraphics on macOS and JUCE's software renderer on Linux, so the distance field differs by
// platform while every glyph, metric and string check agrees.
#include "ProbeRegistry.h"

namespace fcmp::probe
{
    int uiFont(funkgui::test::Probe& P);         // ui_font.cpp
}

FCMP_PROBE(ui, font_linux)
{
    return fcmp::probe::uiFont(P);
}
