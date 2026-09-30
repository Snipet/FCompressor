// Source/editor/Tags.h — FCompressor's primitive tags (02 §3.2, §3.8): product tags >= 256, complete for every view and
// plot of 02 §6–§8 and frozen with Layout.h at FZ4 (K3 #15). A tag is CPU-only: the dump writes its name after a
// primitive ("t HIST_GR"), fingerprints count primitives per tag, and probes find what they check by tag (a curve, a
// meter, a dot) and by the axis recorded under it (Canvas::axis). FunkGui's own tags (1..255: SLOT_LABEL … HINT) cover
// the widgets; these cover the product's plots and chrome.
//
// Values are fixed forever once blessed goldens name them (the dump spells the NAME, so a value never appears in a
// golden, but two builds must agree on the names). Append new tags at the end; never renumber or rename.
// The spellings given by 02 (HIST_IN … COLOUR_CURVE, the LEVEL axis of §3.8's example) are used verbatim; the rest
// follow the same scheme (U1a handoff).
#pragma once

#include <funkgui/canvas/Tags.h>

#include <array>
#include <cstddef>
#include <span>

namespace fcmp::ui::tag
{
    using funkgui::Tag;

    // ---- shared by every plot ------------------------------------------------------------------------------------------
    inline constexpr Tag plotFrame     = 256;   // PLOT_FRAME      a plot's or region's frame
    inline constexpr Tag caption       = 257;   // CAPTION         a plot or group caption (HISTORY, TRANSFER, …)
    inline constexpr Tag grid          = 258;   // GRID            dB / time / frequency grid hairlines, ink16
    inline constexpr Tag axisLabel     = 259;   // AXIS_LABEL      axis numbers and units under a plot
    inline constexpr Tag unity         = 260;   // UNITY           TRANSFER's unity line, COLOUR's identity
    inline constexpr Tag handle        = 261;   // HANDLE          a draggable handle or its locked/derived cross
    inline constexpr Tag unitWord      = 262;   // UNIT_WORD       the "S" / "DB" word after a cell group

    // ---- HISTORY (02 §6.5) -----------------------------------------------------------------------------------------------
    inline constexpr Tag histIn        = 263;   // HIST_IN         AREA floor -> max(inPeakDb), live
    inline constexpr Tag histOut       = 264;   // HIST_OUT        stroke-only AREA at max(outPeakDb), live
    inline constexpr Tag histGr        = 265;   // HIST_GR         hanging GR AREA, live
    inline constexpr Tag histDet       = 266;   // HIST_DET        detector trace, live
    inline constexpr Tag thresholdMark = 267;   // THRESHOLD_MARK  the threshold line at T_in, into the TRANSFER handle
    inline constexpr Tag stateLane     = 268;   // STATE_LANE      phase runs under HISTORY
    inline constexpr Tag freezeCursor  = 269;   // FREEZE_CURSOR   press-and-hold cursor line, ink52
    inline constexpr Tag modeTick      = 270;   // MODE_TICK       1 px ink32 where the history's Mode slot changes
    inline constexpr Tag gap           = 271;   // GAP             an empty span (lap or attach gap)
    inline constexpr Tag histAxis      = 272;   // HIST_AXIS       axis record: x time (s, now = 0), y level (dB)

    // ---- TRANSFER (02 §6.5, §7.3) --------------------------------------------------------------------------------------
    inline constexpr Tag level         = 273;   // LEVEL           axis record: x input level, y output level (dB)
    inline constexpr Tag transferCurve = 274;   // TRANSFER_CURVE  static pre-makeup curve
    inline constexpr Tag grWedge       = 275;   // GR_WEDGE        AREA columns between unity and the curve
    inline constexpr Tag kneeMark      = 276;   // KNEE_MARK       knee hairlines, floor bracket and label
    inline constexpr Tag netCurve      = 277;   // NET_CURVE       with makeup and mix
    inline constexpr Tag ghostCurve    = 278;   // GHOST_CURVE     other detents, the previous Mode's curve
    inline constexpr Tag targetDot     = 279;   // TARGET_DOT      ring at the static target, live
    inline constexpr Tag opDot         = 280;   // OP_DOT          operating dot, live
    inline constexpr Tag grNeedle      = 281;   // GR_NEEDLE       unity -> dot, live
    inline constexpr Tag opTrail       = 282;   // OP_TRAIL        last 320 ms, live
    inline constexpr Tag stageCurve    = 283;   // STAGE_CURVE     stage-1-only curve (Characteristics)

    // ---- METERS (02 §8.8) -------------------------------------------------------------------------------------------------
    inline constexpr Tag meterIn       = 284;   // METER_IN        live
    inline constexpr Tag meterOut      = 285;   // METER_OUT       live
    inline constexpr Tag meterGr       = 286;   // METER_GR        live
    inline constexpr Tag meterSc       = 287;   // METER_SC        live
    inline constexpr Tag meterHold     = 288;   // METER_HOLD      hold ticks, live
    inline constexpr Tag meterReadout  = 289;   // METER_READOUT   max-since-reset numbers
    inline constexpr Tag meterAxis     = 290;   // METER_AXIS      axis record: y level (dB)

    // ---- CONTROL PATH (02 §7.3) ---------------------------------------------------------------------------------------
    inline constexpr Tag cpTarget      = 291;   // CP_TARGET       target GR, live
    inline constexpr Tag cpApplied     = 292;   // CP_APPLIED      applied GR area + min line, live
    inline constexpr Tag cpPhase       = 293;   // CP_PHASE        phase lane, live
    inline constexpr Tag cpInternal    = 294;   // CP_INTERNAL     the Mode's history internal, live
    inline constexpr Tag cpEvents      = 295;   // CP_EVENTS       AUTO SLOW / RANGE / STAGE 2 / FADE stripes, live
    inline constexpr Tag cpAxis        = 296;   // CP_AXIS         axis record: x time, y GR (dB)

    // ---- STEP RESPONSE (02 §7.3) ---------------------------------------------------------------------------------------
    inline constexpr Tag stepCurve     = 297;   // STEP_CURVE      the step responses (min/max columns)
    inline constexpr Tag stepGhost     = 298;   // STEP_GHOST      other detents' nominal curves
    inline constexpr Tag stepSpec      = 299;   // STEP_SPEC       declared spec marker (a draggable time marker)
    inline constexpr Tag stepMeas      = 300;   // STEP_MEAS       measured crossing ring
    inline constexpr Tag stepAxis      = 301;   // STEP_AXIS       axis record: x log time (s), y fraction (0…1)

    // ---- SIDECHAIN | COLOUR (02 §7.3) ----------------------------------------------------------------------------------
    inline constexpr Tag scCurve       = 302;   // SC_CURVE        detector-path response
    inline constexpr Tag scHandle      = 303;   // SC_HANDLE       the SC HPF corner handle
    inline constexpr Tag scAxis        = 304;   // SC_AXIS         axis record: x log f (Hz), y dB
    inline constexpr Tag colourCurve   = 305;   // COLOUR_CURVE    static colour transfer
    inline constexpr Tag colourMark    = 306;   // COLOUR_MARK     ± colourInPeakDb markers, live
    inline constexpr Tag harmonics     = 307;   // HARMONICS       H2 H3 H4 H5 THD bars
    inline constexpr Tag colourAxis    = 308;   // COLOUR_AXIS     axis record: x in, y out (linear)
    inline constexpr Tag tab           = 309;   // TAB             the SIDECHAIN | COLOUR tab cells

    // ---- READOUTS (02 §7.3) ----------------------------------------------------------------------------------------------
    inline constexpr Tag readoutName   = 310;   // READOUT_NAME
    inline constexpr Tag readoutValue  = 311;   // READOUT_VALUE   live

    // ---- chrome: header, display row, footer (02 §6.3, §6.6, §8.5) -----------------------------------------------------
    inline constexpr Tag wordmark      = 312;   // WORDMARK
    inline constexpr Tag topology      = 313;   // TOPOLOGY        ModeDescriptor::topologyLine
    inline constexpr Tag modeLatch     = 314;   // MODE_LATCH      caption, chevrons and name cell
    inline constexpr Tag modeName      = 315;   // MODE_NAME
    inline constexpr Tag modeGroup     = 316;   // MODE_GROUP      "VCA · 2 OF 8"
    inline constexpr Tag presetStrip   = 317;   // PRESET_STRIP
    inline constexpr Tag displayCaption = 318;  // DISPLAY_CAPTION
    inline constexpr Tag displayValue  = 319;   // DISPLAY_VALUE   the big readout (live when it shows GR)
    inline constexpr Tag displaySub    = 320;   // DISPLAY_SUB
    inline constexpr Tag footerSpec    = 321;   // FOOTER_SPEC     the spec line
    inline constexpr Tag notice        = 322;   // NOTICE          state notices and the Mode-switch summary

    // ---- slots beyond FunkGui's (02 §6.4) ------------------------------------------------------------------------------
    inline constexpr Tag detTick       = 323;   // DET_TICK        THRESHOLD's 1×4 detector tick, live
    inline constexpr Tag rangeBar      = 324;   // RANGE_BAR       RANGE's GR-used bar, live

    // ---- browsers (02 §8.6, U6) --------------------------------------------------------------------------------------------
    inline constexpr Tag browserBg     = 325;   // BROWSER_BG      the overlay's opaque ground
    inline constexpr Tag browserHeading = 326;  // BROWSER_HEADING a group column heading
    inline constexpr Tag browserRow    = 327;   // BROWSER_ROW     a Mode or preset row
    inline constexpr Tag browserCurrent = 328;  // BROWSER_CURRENT the current row's bar
    inline constexpr Tag browserPager  = 329;   // BROWSER_PAGER   ‹ 1/2 ›

    // ---- v1.1 (ADR-74, ADR-75) -------------------------------------------------------------------------------------------
    inline constexpr Tag linkedBox     = 330;   // LINKED_BOX      the box around a linked (derived) slot
    inline constexpr Tag modeRule      = 331;   // MODE_RULE       the Mode-coloured rule under the Mode name
    inline constexpr Tag modeSwatch    = 332;   // MODE_SWATCH     a Mode browser row's colour swatch
    inline constexpr Tag meterFace     = 333;   // METER_FACE      a GR meter's hardware plate, shroud, unlit LEDs (ADR-76)
    // ---- v1.2 (ADR-81) ---------------------------------------------------------------------------------------------------
    inline constexpr Tag meterDetail   = 334;   // METER_DETAIL    a meter plate's detail: screws, glass, shadows, the %
                                                //                 scale, fine ticks, LED glow (live where it moves)

    // ---- v1.2 (ADR-85) ---------------------------------------------------------------------------------------------------
    inline constexpr Tag settingsBg    = 335;   // SETTINGS_BG     the settings overlay's ground, rules and divider
    inline constexpr Tag settingsText  = 336;   // SETTINGS_TEXT   its headings, captions, tables and notes
    inline constexpr Tag settingsValue = 337;   // SETTINGS_VALUE  a DIAGNOSTICS value (live where it moves)
    inline constexpr Tag settingsGear  = 338;   // SETTINGS_GEAR   the header's gear, and COPY REPORT

    // ---- v1.2 (ADR-88) ---------------------------------------------------------------------------------------------------
    inline constexpr Tag outputTrim    = 339;   // OUTPUT_TRIM     the display row's OUTPUT caption, rule, notch and caret
    inline constexpr Tag outputValue   = 340;   // OUTPUT_VALUE    its value text
    // ---- v1.2 (ADR-89) ---------------------------------------------------------------------------------------------------
    inline constexpr Tag valueEntry    = 341;   // VALUE_ENTRY     a typed-value field: its box, text, selection, caret

    inline constexpr Tag last          = 341;

    inline constexpr std::array<funkgui::TagName, last - funkgui::tags::firstProduct + 1> kNames { {
        { plotFrame, "PLOT_FRAME" },         { caption, "CAPTION" },             { grid, "GRID" },
        { axisLabel, "AXIS_LABEL" },         { unity, "UNITY" },                 { handle, "HANDLE" },
        { unitWord, "UNIT_WORD" },
        { histIn, "HIST_IN" },               { histOut, "HIST_OUT" },            { histGr, "HIST_GR" },
        { histDet, "HIST_DET" },             { thresholdMark, "THRESHOLD_MARK" }, { stateLane, "STATE_LANE" },
        { freezeCursor, "FREEZE_CURSOR" },   { modeTick, "MODE_TICK" },          { gap, "GAP" },
        { histAxis, "HIST_AXIS" },
        { level, "LEVEL" },                  { transferCurve, "TRANSFER_CURVE" }, { grWedge, "GR_WEDGE" },
        { kneeMark, "KNEE_MARK" },           { netCurve, "NET_CURVE" },          { ghostCurve, "GHOST_CURVE" },
        { targetDot, "TARGET_DOT" },         { opDot, "OP_DOT" },                { grNeedle, "GR_NEEDLE" },
        { opTrail, "OP_TRAIL" },             { stageCurve, "STAGE_CURVE" },
        { meterIn, "METER_IN" },             { meterOut, "METER_OUT" },          { meterGr, "METER_GR" },
        { meterSc, "METER_SC" },             { meterHold, "METER_HOLD" },        { meterReadout, "METER_READOUT" },
        { meterAxis, "METER_AXIS" },
        { cpTarget, "CP_TARGET" },           { cpApplied, "CP_APPLIED" },        { cpPhase, "CP_PHASE" },
        { cpInternal, "CP_INTERNAL" },       { cpEvents, "CP_EVENTS" },          { cpAxis, "CP_AXIS" },
        { stepCurve, "STEP_CURVE" },         { stepGhost, "STEP_GHOST" },        { stepSpec, "STEP_SPEC" },
        { stepMeas, "STEP_MEAS" },           { stepAxis, "STEP_AXIS" },
        { scCurve, "SC_CURVE" },             { scHandle, "SC_HANDLE" },          { scAxis, "SC_AXIS" },
        { colourCurve, "COLOUR_CURVE" },     { colourMark, "COLOUR_MARK" },      { harmonics, "HARMONICS" },
        { colourAxis, "COLOUR_AXIS" },       { tab, "TAB" },
        { readoutName, "READOUT_NAME" },     { readoutValue, "READOUT_VALUE" },
        { wordmark, "WORDMARK" },            { topology, "TOPOLOGY" },           { modeLatch, "MODE_LATCH" },
        { modeName, "MODE_NAME" },           { modeGroup, "MODE_GROUP" },        { presetStrip, "PRESET_STRIP" },
        { displayCaption, "DISPLAY_CAPTION" }, { displayValue, "DISPLAY_VALUE" }, { displaySub, "DISPLAY_SUB" },
        { footerSpec, "FOOTER_SPEC" },       { notice, "NOTICE" },
        { detTick, "DET_TICK" },             { rangeBar, "RANGE_BAR" },
        { browserBg, "BROWSER_BG" },         { browserHeading, "BROWSER_HEADING" }, { browserRow, "BROWSER_ROW" },
        { browserCurrent, "BROWSER_CURRENT" }, { browserPager, "BROWSER_PAGER" },
        { linkedBox, "LINKED_BOX" },         { modeRule, "MODE_RULE" },          { modeSwatch, "MODE_SWATCH" },
        { meterFace, "METER_FACE" },         { meterDetail, "METER_DETAIL" },
        { settingsBg, "SETTINGS_BG" },       { settingsText, "SETTINGS_TEXT" },  { settingsValue, "SETTINGS_VALUE" },
        { settingsGear, "SETTINGS_GEAR" },
        { outputTrim, "OUTPUT_TRIM" },       { outputValue, "OUTPUT_VALUE" },   { valueEntry, "VALUE_ENTRY" },
    } };

    namespace detail
    {
        constexpr bool validName(const char* s) noexcept
        {
            if (s == nullptr || *s == '\0')
                return false;
            for (; *s != '\0'; ++s)
                if (!((*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_'))
                    return false;
            return true;
        }

        constexpr bool sameName(const char* a, const char* b) noexcept
        {
            for (; *a != '\0' && *a == *b; ++a, ++b) {}
            return *a == *b;
        }

        // Every product tag from 256 to `last` is named exactly once, in order, with a legal and unique dump name
        // that no FunkGui tag uses.
        constexpr bool namesAreComplete() noexcept
        {
            for (std::size_t i = 0; i < kNames.size(); ++i)
            {
                if (kNames[i].tag != static_cast<Tag>(funkgui::tags::firstProduct + i) || !validName(kNames[i].name))
                    return false;
                for (std::size_t j = 0; j < i; ++j)
                    if (sameName(kNames[i].name, kNames[j].name))
                        return false;
                for (const funkgui::TagName& f : funkgui::kFunkGuiTagNames)
                    if (sameName(kNames[i].name, f.name))
                        return false;
            }
            return true;
        }
    }
    static_assert(detail::namesAreComplete(), "Tags.h: every product tag is named once, in order, [A-Z0-9_]+");

    // Registers the names with FunkGui's dump (message thread; idempotent). The Panel's constructor calls it, so every
    // dump a Panel's frame produces or parses spells product tags by name.
    inline void registerTagNames() { funkgui::registerTagNames(std::span<const funkgui::TagName>(kNames)); }
}
