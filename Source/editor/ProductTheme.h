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
//
// Mode colours (ADR-75, v1.1; the user asked for each compressor type to have its own colour). Every Mode has one
// colour per theme, and it replaces the theme's `signal` token, the ink of live gain-reduction data (the GR readout
// and its live bar, the VU needle, the GR meter, the history's GR trace, the operating dot, RANGE's bar), so a
// Mode's live picture takes its colour with no conditional anywhere in the views. The header's rule under the Mode name
// and a swatch per row of the Mode browser draw the same colour, so the colour and the Mode are learnt together. The hues
// sit around the wheel away from the accent's vermilion (the control under the hand), so the two never read alike.
// WCAG ratio against the ground (targets: GRAPHITE >= 7, PAPER >= 5.5, ADR-73's signal target; ui.contrast checks):
//   Mode        hue    GRAPHITE           PAPER
//   fet-76       45°   FFC857  11.65      7A4E00  6.04     amber
//   opto-2a      80°   C6E86B  12.94      4A6200  5.80     lime
//   mu-67       135°   7EE08F  11.05      17662F  5.92     green
//   clean       190°   7FD4E8  10.66      0A5668  6.93     cyan (FunkGui's GRAPHITE signal; ADR-73's PAPER signal)
//   bus-g       215°   8CB8FF   8.89      1C4A9E  6.99     azure
//   bus-25      245°   A9A5FF   8.12      4238A8  7.46     periwinkle
//   diode-609   275°   D69EFF   8.70      6E2FA3  6.78     violet
//   brickwall   320°   FF92D4   8.80      962470  6.33     magenta
//   octo        350°   FF8FA3   8.29      A3243F  6.12     rose (v1.2, wave 2)
// A Mode not in the table (a later wave before it gets its own) draws Clean's colour.
#pragma once

#include <funkgui/core/Theme.h>

#include <array>
#include <string_view>

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

    // ADR-75: each Mode's colour in GRAPHITE and in PAPER (see above).
    struct ModeColour
    {
        std::string_view key;
        funkgui::Col     graphite, paper;
    };
    inline constexpr std::array<ModeColour, 14> kModeColours { {
        { "clean",     { 0x7F, 0xD4, 0xE8 }, { 0x0A, 0x56, 0x68 } },
        { "bus-g",     { 0x8C, 0xB8, 0xFF }, { 0x1C, 0x4A, 0x9E } },
        { "fet-76",    { 0xFF, 0xC8, 0x57 }, { 0x7A, 0x4E, 0x00 } },
        { "opto-2a",   { 0xC6, 0xE8, 0x6B }, { 0x4A, 0x62, 0x00 } },
        { "mu-67",     { 0x7E, 0xE0, 0x8F }, { 0x17, 0x66, 0x2F } },
        { "diode-609", { 0xD6, 0x9E, 0xFF }, { 0x6E, 0x2F, 0xA3 } },
        { "bus-25",    { 0xA9, 0xA5, 0xFF }, { 0x42, 0x38, 0xA8 } },
        { "brickwall", { 0xFF, 0x92, 0xD4 }, { 0x96, 0x24, 0x70 } },
        { "octo",      { 0xFF, 0x8F, 0xA3 }, { 0xA3, 0x24, 0x3F } },
        { "console-e", { 0x5C, 0xE0, 0xC4 }, { 0x00, 0x66, 0x55 } },
        { "opto-3a",   { 0xA6, 0xF0, 0x7A }, { 0x35, 0x66, 0x0C } },
        { "diode-54",  { 0xEE, 0x9C, 0xF2 }, { 0x7E, 0x22, 0x86 } },
        { "mu-mastering", { 0x6E, 0xE8, 0xA8 }, { 0x0F, 0x6B, 0x40 } },
        { "opto-tube-1b", { 0xFF, 0xA0, 0x70 }, { 0x9A, 0x40, 0x10 } },
    } };

    // A light-ground theme (PAPER) takes a Mode colour's dark variant.
    constexpr bool isLightTheme(const funkgui::Theme& th) noexcept
    {
        return static_cast<int>(th.ground.r) + th.ground.g + th.ground.b > 3 * 128;
    }

    // The colour of Mode `key` on theme `th`; Clean's for a key the table does not name.
    constexpr funkgui::Col modeColour(std::string_view key, const funkgui::Theme& th) noexcept
    {
        const ModeColour* found = &kModeColours[0];
        for (const ModeColour& m : kModeColours)
            if (m.key == key)
                found = &m;
        return isLightTheme(th) ? found->paper : found->graphite;
    }

    // The palette FCompressor draws theme index `index` with: its own PAPER for 1, FunkGui's theme otherwise
    // (GRAPHITE for 0 and for an index that names no theme, as funkgui::Theme::byIndex).
    constexpr funkgui::Theme productTheme(int index) noexcept
    {
        return index == kThemePaper ? paperHighContrast() : funkgui::Theme::byIndex(index);
    }
}
