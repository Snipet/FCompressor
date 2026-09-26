// Source/editor/ProductTheme.h — the palettes FCompressor draws with (ADR-73, S13 H1a).
//
// The user asked for the white theme's contrast to be "majorly increased" (ADR-73). FunkGui's Theme::paper() is
// HardwareReverb's palette and HR's goldens hold it, so FCompressor does not change it: the Panel maps theme index 1
// (PAPER) to its own high-contrast palette below and hands that to every sub-view, and the preset menus take the same
// palette (productTheme). GRAPHITE (index 0) is FunkGui's own, unchanged. FunkGui itself changes nothing.
//
// The high-contrast PAPER (WCAG 2.x contrast ratio against the ground; ADR-73's targets in brackets, the old PAPER's
// ratios after them; the probe ui.contrast checks every one):
//   ground     EDEBE6   the old PAPER's, unchanged: EditorHost's clear colour and fallback screen still match
//   ink100     0B0C0E   16.4  [>= 15 ]  old 14.9
//   ink70      272A2F   12.1  [>= 9  ]  old  5.5
//   ink52      3E4146    8.6  [>= 6  ]  old  3.8
//   ink32      5A5D62    5.5  [>= 3.5]  old  1.8
//   ink16      AAACB0    1.9  [>= 1.8]  old  1.3
//   accent     AC330C    5.4  [>= 4.5]  old  3.3
//   accentDim  D47E60    2.5  [>= 2.2]  old  1.4
//   signal     0A5668    6.9  [>= 5.5]  old  3.9
// Every level is stronger than GRAPHITE's (ink70 9.0, ink52 5.5, ink32 2.7, ink16 1.5, accentDim 2.1), and the ladder
// stays strictly ordered (ink100 > ink70 > ink52 > ink32 > ink16; L* 3, 17, 27, 39, 70 on a ground of 93). The inks
// keep the old PAPER's cool neutral cast, its vermilion accent and its teal signal, each darker. The text levels
// (ink32 up) carry the most: thin dark glyphs on a light ground read lighter than their ink, so ink32 (axis labels,
// detents, at-default values, the spec line) went from 1.8 to 5.5. ink16 is also a fill (latch off, the selected
// preset row, the GR wedge), so it stays just above its target, where ink52 text on it still reads at 4.5.
// Known cost: FunkGui's LatchToggle draws a pressed latch's label in accent on an ink32 fill, which is about 1:1 here
// (it was 1.8); the press lasts only while the button is held.
//
// textGamma. The SDF text coverage is raised to textGamma before it composites (in gamma-encoded space; FunkGui's
// fs_ui.sc and SoftRaster). FunkGui's PAPER thins dark text with 1.4, so thin kLabel and kMicro glyphs read lighter
// than their ink; tuned by eye on the PNGs, 0.8 fills them out towards the ink without blooming (GRAPHITE fattens its
// light text with 1/1.4). The host records its own theme's textGamma into the frame (EditorHost, HeadlessHost), so
// Panel::draw begins the still-empty frame again with this value (Canvas::begin; Panel.cpp) whenever it draws PAPER.
#pragma once

#include <funkgui/core/Theme.h>

namespace fcmp::ui
{
    inline constexpr int kThemeGraphite = 0;                     // funkgui::Theme::byIndex / HostServices::themeIndex
    inline constexpr int kThemePaper    = 1;

    // ADR-73: FCompressor's high-contrast PAPER (see above).
    constexpr funkgui::Theme paperHighContrast() noexcept
    {
        return { { 0xED, 0xEB, 0xE6 },                           // ground (FunkGui's PAPER ground)
                 { 0x0B, 0x0C, 0x0E },                           // ink100
                 { 0x27, 0x2A, 0x2F },                           // ink70
                 { 0x3E, 0x41, 0x46 },                           // ink52
                 { 0x5A, 0x5D, 0x62 },                           // ink32
                 { 0xAA, 0xAC, 0xB0 },                           // ink16
                 { 0xAC, 0x33, 0x0C },                           // accent
                 { 0xD4, 0x7E, 0x60 },                           // accentDim
                 { 0x0A, 0x56, 0x68 },                           // signal
                 0.8f };                                          // textGamma
    }

    // The palette FCompressor draws theme index `index` with: its own PAPER for 1, FunkGui's theme otherwise
    // (GRAPHITE for 0 and for an index that names no theme, as funkgui::Theme::byIndex).
    constexpr funkgui::Theme productTheme(int index) noexcept
    {
        return index == kThemePaper ? paperHighContrast() : funkgui::Theme::byIndex(index);
    }
}
