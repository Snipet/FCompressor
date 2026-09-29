// Source/editor/Layout.h — every layout constant of the FCompressor panel (02 §6–§7, with the browser of §8.6 and the
// meters of §8.8), written complete by U1a and frozen at FZ4 (K3 #15). Logical px of the fixed 960×640 window; seconds
// for times. Views, plots and probes read positions from here and nowhere else, so a later card never edits the
// composer (Panel.cpp) or another card's view to move something.
//
// Q4 (S5 base): the Characteristics screen keeps the chrome — the header (y 0–60), the display row (y 64–120) and the
// footer (y 604–616) are identical on both screens; only the middle region y 124–600 swaps (02 §7.1).
//
// Values the design states are transcribed as given. Where 02 gives a rule instead of a number, the number derived
// here is the one every card uses (U1a handoff):
// - Cell groups written "right-aligned to x" (the HISTORY span cells, the TRANSFER scale cells) follow the display
//   row's cells (QUALITY, LOOKAHEAD): 16 px tall, 4 px above the caption line (caption top 126 -> cells y 122), cell
//   width = kCaption text width + 14 rounded to an even number (ECO 32, HQ 26, 5 MS 38: the given cells obey it), a
//   4 px gap between cells, and the unit word ("S", "DB") right-aligned at x with 3 px between it and the last cell.
// - The Characteristics captions of the second plot row sit at y 408, 4 px under the SC|COLOUR tab cells (y 404), as
//   the band's captions (126) sit 4 px under their cells (122).
// - Each region's rectangle (kHeader, kDisplayRow, ..., kBand) is the union of what 02 draws there; the regions do not
//   overlap except the header/preset strip (the strip is hit-tested first) and the browsers, which are overlays.
// - The Characteristics METERS have no numeric hold readouts in 02 §7.3, but §7.4 resets their holds by a click: the
//   reset target is the METERS caption cell (kCharsMeters.reset).
// - S9 lead revision 5a (U3): the Characteristics meter labels are not centred on their 16 px bar pitch, where kMicro's
//   "GR" (10.5 px) and "OUT" (16.4 px) sat 2.5 px apart and read "GROUT". They keep >= 6 px between words (a word space
//   is 7.35 px, a letter gap 1.4 px) within 2.5 px of their bars: IN 736, SC 752.5, GR 769, OUT 788.5. Their row moves
//   from y 390 (TRANSFER's label row) to y 381, right under the bars: TRANSFER's "0 DB <LAW>" starts on the 0 dB tick
//   and runs to x 731 ("RMS"), 737 ("TUBE") at S = 48 and 749 at S = 72, into the meters' column, so on one row the two
//   would touch. Probe ui.charscreen meters.labels.*. Only these five values of kCharsMeters moved.
#pragma once

#include "fcdsp/params/Pid.h"

#include <funkgui/core/Geometry.h>
#include <funkgui/widgets/RuleSlider.h>          // SlotGeom, SlotSize

#include <array>
#include <cstdint>

namespace fcmp::ui::layout
{
    using funkgui::Point;
    using funkgui::Rect;

    // ---- window and chrome (02 §6.1) ----------------------------------------------------------------------------------
    inline constexpr int   kWidth  = 960;                       // fixed: setResizable(false, false), setSize once
    inline constexpr int   kHeight = 640;
    inline constexpr float kMargin = 40.0f;
    inline constexpr float kContentLeft  = 40.0f;               // content x 40–920
    inline constexpr float kContentRight = 920.0f;
    inline constexpr float kContentWidth = kContentRight - kContentLeft;

    inline constexpr Rect kHeader     { 40.0f, 0.0f, 880.0f, 60.0f };        // y 0–60 (never moves, both screens)
    inline constexpr Rect kDisplayRow { 40.0f, 62.0f, 880.0f, 58.0f };       // y 64–120; its cells start at y 62
    inline constexpr Rect kFooter     { 40.0f, 600.0f, 880.0f, 20.0f };      // text y 604, THEME cells y 601–617
    inline constexpr float kMiddleTop    = 124.0f;                           // the region that swaps (02 §7.1)
    inline constexpr float kMiddleBottom = 600.0f;
    inline constexpr Rect kMiddle     { 40.0f, 124.0f, 880.0f, 476.0f };
    inline constexpr Rect kOverlay    { 40.0f, 64.0f, 880.0f, 286.0f };      // Mode and preset browsers: y 64–350

    // ---- header (02 §6.3, §8.5) ---------------------------------------------------------------------------------------
    namespace header
    {
        inline constexpr Point kWordmark { 40.0f, 18.0f };      // "F" kWordmark ink100, then "COMPRESSOR" ink52
        inline constexpr float kWordmarkGap = 9.0f;             // "COMPRESSOR" at x = 40 + w("F") + 9
        inline constexpr Point kTopology { 40.0f, 40.0f };      // ModeDescriptor::topologyLine, kCaption ink32
        inline constexpr Rect  kPresetStrip { 228.0f, 12.0f, 372.0f, 44.0f };   // HR's strip, 24 px right, 2 px up
        inline constexpr float kModeCaptionRight = 648.0f;      // "MODE", kCaption ink52, right-aligned
        inline constexpr float kModeCaptionTop   = 22.0f;
        inline constexpr Rect  kModePrev { 656.0f, 14.0f, 24.0f, 32.0f };       // ‹ (2 segments)
        inline constexpr Rect  kModeName { 680.0f, 14.0f, 216.0f, 32.0f };      // click opens the Mode browser
        inline constexpr float kModeNameTextX   = 688.0f;       // kLatch ink100 on capCentreTop(kModeNameCentreY)
        inline constexpr float kModeNameCentreY = 30.0f;
        inline constexpr Rect  kModeNext { 896.0f, 14.0f, 24.0f, 32.0f };       // › (2 segments)
        inline constexpr Point kModeGroup { 688.0f, 48.0f };    // "VCA · 2 OF 8", kMicro ink32
        inline constexpr Rect  kModeLatch { 600.0f, 14.0f, 320.0f, 42.0f };     // caption + ‹ name › + group line
    }

    // ---- display row (02 §6.3, §6.6) ----------------------------------------------------------------------------------
    namespace display
    {
        inline constexpr Point kCaption { 40.0f, 66.0f };       // kCaption ink52: GAIN REDUCTION or the slot's label
        inline constexpr Point kValue   { 40.0f, 78.0f };       // kDisplay 44 + kUnit 14 on the shared baseline
        inline constexpr float kSubX       = 224.0f;            // sub-readout: left x, cap-centred y, max width
        inline constexpr float kSubCentreY = 100.0f;
        inline constexpr float kSubMaxW    = 148.0f;
        inline constexpr Point kQualityCaption { 380.0f, 66.0f };
        inline constexpr std::array<Rect, 3> kQualityCells { { { 446.0f, 62.0f, 32.0f, 16.0f },       // ECO
                                                               { 482.0f, 62.0f, 32.0f, 16.0f },       // STD
                                                               { 518.0f, 62.0f, 26.0f, 16.0f } } };   // HQ
        inline constexpr Point kLookaheadCaption { 380.0f, 92.0f };
        inline constexpr std::array<Rect, 3> kLookaheadCells { { { 446.0f, 88.0f, 32.0f, 16.0f },     // OFF
                                                                 { 482.0f, 88.0f, 38.0f, 16.0f },     // 5 MS
                                                                 { 524.0f, 88.0f, 44.0f, 16.0f } } }; // 20 MS
        inline constexpr Rect  kDelta           { 588.0f, 72.0f, 88.0f, 36.0f };   // kLatch 13
        inline constexpr Rect  kBypass          { 684.0f, 72.0f, 96.0f, 36.0f };
        inline constexpr Rect  kCharacteristics { 788.0f, 72.0f, 132.0f, 36.0f };  // UI state, not a parameter
        inline constexpr float kDwellS = 0.9f;                  // a touched slot stays on the display 0.9 s (HR)
        inline constexpr float kGrShownDb = 0.05f;              // GAIN REDUCTION in `signal` when live and above this
    }

    // ---- v1.1 (ADR-74, ADR-75) ------------------------------------------------------------------------------------------
    // ADR-75: a Mode change eases the Mode colour (the signal ink) over this long (smootherstep).
    inline constexpr float kModeColourS = 0.25f;
    // ADR-74: a linked (derived) slot sits in a box: the slot's hit rectangle (SlotGeom::hit, x−6 … x+w+6) with this
    // corner radius, a 1 px ink32 border and an ink16 fill at kLinkedFillAlpha, drawn under the slot.
    inline constexpr float kLinkedRadius = 4.0f;
    inline constexpr float kLinkedFillAlpha = 0.35f;
    // ADR-75: the header's rule under the Mode name is the Mode colour, this thick; the Mode browser's swatch per row
    // is a disc of this radius, centred kSwatchInset px after the end of the row's name.
    inline constexpr float kModeRuleH = 2.0f;
    inline constexpr float kSwatchR = 3.0f;
    inline constexpr float kSwatchInset = 11.0f;

    // ---- footer (02 §6.3, §6.6) ---------------------------------------------------------------------------------------
    namespace footer
    {
        inline constexpr Point kSpec { 40.0f, 604.0f };         // spec line, kLabel ink32, fitEllipsis to kSpecMaxW
        inline constexpr float kSpecMaxW = 740.0f;              // retired by UF1b (the ZOOM cells): kSpecLineW below
        inline constexpr std::array<Rect, 2> kThemeCells { { { 790.0f, 601.0f, 72.0f, 16.0f },        // GRAPHITE
                                                             { 866.0f, 601.0f, 54.0f, 16.0f } } };    // PAPER
        inline constexpr float kHintS    = 6.0f;                // first-run hint
        inline constexpr float kNoticeS  = 10.0f;               // state notice after a load (01 §9.1)
        inline constexpr float kPoisonS  = 3.0f;                // AUDIO RESET AFTER A NON-FINITE SAMPLE
        inline constexpr float kSummaryS = 3.0f;                // Mode-switch summary (02 §8.7)
    }

    // ---- slots (02 §6.1, §6.4) ----------------------------------------------------------------------------------------
    inline constexpr int   kSlotCols  = 7;
    inline constexpr int   kSlotRows  = 3;
    inline constexpr int   kSlotCount = kSlotCols * kSlotRows;  // 21
    inline constexpr float kSlotX0    = 40.0f;                  // x_i = 40 + 128·i
    inline constexpr float kSlotPitch = 128.0f;
    inline constexpr float kSlotW     = 112.0f;                 // hit {x−6, top−6, 124, h}: a 4 px dead gutter

    enum class SlotRow : uint8_t { p, b, c };                   // primary P, secondary B and C
    inline constexpr std::array<float, 3> kRowTop { 362.0f, 450.0f, 522.0f };   // label lines

    constexpr float slotX(int col) noexcept { return kSlotX0 + kSlotPitch * static_cast<float>(col); }

    constexpr funkgui::SlotGeom slotGeom(SlotRow row, int col) noexcept
    {
        return { slotX(col), kRowTop[static_cast<std::size_t>(row)], kSlotW,
                 row == SlotRow::p ? funkgui::SlotSize::primary : funkgui::SlotSize::secondary };
    }

    // One slot of the fixed grid. `label` is the universal name (the aka when a Mode renames the slot, 02 §8.1);
    // `word` is the attached word's parameter (kNoPid: none). `bipolar` slots fill from the default notch (§8.1).
    struct SlotPlace
    {
        SlotRow     row;
        int         col;
        fcdsp::Pid  pid;
        const char* label;
        fcdsp::Pid  word;
        const char* wordLabel;
        bool        bipolar;
    };

    // 02 §6.4: all 22 Mode-filtered parameters placed (automu is the AUTO word on MAKEUP), no compound cell.
    inline constexpr std::array<SlotPlace, kSlotCount> kSlots { {
        { SlotRow::p, 0, fcdsp::Pid::thr,    "THRESHOLD",  fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::p, 1, fcdsp::Pid::ratio,  "RATIO",      fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::p, 2, fcdsp::Pid::knee,   "KNEE",       fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::p, 3, fcdsp::Pid::atk,    "ATTACK",     fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::p, 4, fcdsp::Pid::rel,    "RELEASE",    fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::p, 5, fcdsp::Pid::makeup, "MAKEUP",     fcdsp::Pid::automu,  "AUTO",   true  },
        { SlotRow::p, 6, fcdsp::Pid::mix,    "MIX",        fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::b, 0, fcdsp::Pid::range,  "RANGE",      fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::b, 1, fcdsp::Pid::det,    "DETECT",     fcdsp::Pid::extkey,  "EXT",    false },
        { SlotRow::b, 2, fcdsp::Pid::hold,   "HOLD",       fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::b, 3, fcdsp::Pid::look,   "LOOKAHEAD",  fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::b, 4, fcdsp::Pid::tmode,  "TIME MODE",  fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::b, 5, fcdsp::Pid::drive,  "DRIVE",      fcdsp::kNoPid,       nullptr,  true  },
        { SlotRow::b, 6, fcdsp::Pid::voice,  "VOICE",      fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::c, 0, fcdsp::Pid::schpf,  "SC HPF",     fcdsp::Pid::listen,  "LISTEN", false },
        { SlotRow::c, 1, fcdsp::Pid::sce,    "SC EMPH",    fcdsp::kNoPid,       nullptr,  true  },
        { SlotRow::c, 2, fcdsp::Pid::link,   "LINK",       fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::c, 3, fcdsp::Pid::stmode, "STEREO",     fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::c, 4, fcdsp::Pid::s2thr,  "S2 THRESH",  fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::c, 5, fcdsp::Pid::s2atk,  "S2 ATTACK",  fcdsp::kNoPid,       nullptr,  false },
        { SlotRow::c, 6, fcdsp::Pid::s2rel,  "S2 RELEASE", fcdsp::kNoPid,       nullptr,  false },
    } };

    // The slot showing `pid` (nullptr for automu, which is a word, and for the globals).
    constexpr const SlotPlace* slotOf(fcdsp::Pid pid) noexcept
    {
        for (const SlotPlace& s : kSlots)
            if (s.pid == pid)
                return &s;
        return nullptr;
    }

    // The universal (upper-case) name of a Mode-filtered parameter: its slot's label, or the word for automu.
    constexpr const char* universalLabel(fcdsp::Pid pid) noexcept
    {
        if (const SlotPlace* s = slotOf(pid))
            return s->label;
        for (const SlotPlace& s : kSlots)
            if (s.word == pid)
                return s.wordLabel;
        return "";
    }

    constexpr funkgui::SlotGeom slotGeom(const SlotPlace& s) noexcept { return slotGeom(s.row, s.col); }

    // The union of the 21 slot hit rectangles (y 356–582).
    inline constexpr Rect kSlotGrid { kSlotX0 - 6.0f, kRowTop[0] - 6.0f,
                                      kSlotPitch * (kSlotCols - 1) + kSlotW + 12.0f, 582.0f - (kRowTop[0] - 6.0f) };

    // ---- the shared level map (02 §6.1, §7.3) -------------------------------------------------------------------------
    inline constexpr float kLevelTopDb = 6.0f;                  // the top of every level axis: +6 dBFS
    inline constexpr std::array<int, 4> kScalesDb { 12, 24, 48, 72 };           // meterScaleDb preference
    inline constexpr int   kDefaultScaleDb = 48;
    inline constexpr std::array<int, 4> kSpansTenths { 25, 50, 100, 200 };     // historySpanTenths preference (s·10)
    inline constexpr int   kDefaultSpanTenths = 50;

    // y(db) = top + (6 − db)·height/S: the band's plots share top 140 and height 192 (4 px/dB at S = 48), the
    // Characteristics level row top 140 and height 240 (5 px/dB). The floor is 6 − S.
    struct LevelMap
    {
        float top = 0.0f, height = 0.0f;
        constexpr float y(float db, float scaleDb) const noexcept { return top + (kLevelTopDb - db) * height / scaleDb; }
        constexpr float db(float py, float scaleDb) const noexcept { return kLevelTopDb - (py - top) * scaleDb / height; }
        constexpr float pxPerDb(float scaleDb) const noexcept { return height / scaleDb; }
    };
    inline constexpr LevelMap kBandLevel  { 140.0f, 192.0f };
    inline constexpr LevelMap kCharsLevel { 140.0f, 240.0f };

    // ---- plot geometry: every plot is parameterised by one of these, so the band and the Characteristics screen draw
    //      the same visual aids with the same code (02 Part 2 intro) --------------------------------------------------

    // HISTORY (02 §6.5, §7.3): time runs leftwards, "now" is the right edge.
    struct HistoryGeom
    {
        Rect                plot;
        int                 columns;         // 250 | 210 columns of colWidth px
        float               colWidth;
        LevelMap            level;
        Point               caption;         // "HISTORY", kCaption ink52
        std::array<Rect, 4> spanCells;       // 2.5 · 5 · 10 · 20
        float               unitRight;       // "S" right-aligned here
        Rect                stateLane;
        float               timeLabelY;      // time labels, every timeLabelPitch px from the right edge
        float               timeLabelPitch;
        bool                alwaysDet;       // HIST_DET always drawn (Characteristics) or only when it can differ
    };
    inline constexpr HistoryGeom kBandHistory {
        { 40.0f, 140.0f, 500.0f, 192.0f }, 250, 2.0f, kBandLevel, { 40.0f, 126.0f },
        { { { 418.0f, 122.0f, 32.0f, 16.0f }, { 454.0f, 122.0f, 18.0f, 16.0f },
            { 476.0f, 122.0f, 26.0f, 16.0f }, { 506.0f, 122.0f, 26.0f, 16.0f } } },
        540.0f, { 40.0f, 334.0f, 500.0f, 3.0f }, 340.0f, 100.0f, false };
    inline constexpr HistoryGeom kCharsHistory {
        { 40.0f, 140.0f, 420.0f, 240.0f }, 210, 2.0f, kCharsLevel, { 40.0f, 126.0f },
        { { { 338.0f, 122.0f, 32.0f, 16.0f }, { 374.0f, 122.0f, 18.0f, 16.0f },
            { 396.0f, 122.0f, 26.0f, 16.0f }, { 426.0f, 122.0f, 26.0f, 16.0f } } },
        460.0f, { 40.0f, 383.0f, 420.0f, 3.0f }, 390.0f, 84.0f, true };

    // TRANSFER (02 §6.5, §7.3): square, x(db) = plot.x + (db − 6 + S)·plot.w/S, y from the level map.
    struct TransferGeom
    {
        Rect                plot;
        LevelMap            level;
        Point               caption;         // "TRANSFER"
        std::array<Rect, 4> scaleCells;      // 12 · 24 · 48 · 72
        float               unitRight;       // "DB" right-aligned here
        float               labelY;          // dB labels and the unit/detector law under the plot
        float               kneeBracketY;    // the knee floor bracket (1×4 ink32 end ticks)
        bool                stageCurve;      // draw the stage-1-only curve of two-stage Modes (Characteristics)
        bool                handleStops;     // handles are Tab stops (Characteristics); on PANEL the slots are
    };
    inline constexpr TransferGeom kBandTransfer {
        { 556.0f, 140.0f, 192.0f, 192.0f }, kBandLevel, { 556.0f, 126.0f },
        { { { 618.0f, 122.0f, 26.0f, 16.0f }, { 648.0f, 122.0f, 26.0f, 16.0f },
            { 678.0f, 122.0f, 26.0f, 16.0f }, { 708.0f, 122.0f, 26.0f, 16.0f } } },
        748.0f, 340.0f, 328.0f, false, false };
    inline constexpr TransferGeom kCharsTransfer {
        { 476.0f, 140.0f, 240.0f, 240.0f }, kCharsLevel, { 476.0f, 126.0f },
        { { { 586.0f, 122.0f, 26.0f, 16.0f }, { 616.0f, 122.0f, 26.0f, 16.0f },
            { 646.0f, 122.0f, 26.0f, 16.0f }, { 676.0f, 122.0f, 26.0f, 16.0f } } },
        716.0f, 390.0f, 376.0f, true, true };

    constexpr float transferX(const TransferGeom& g, float db, float scaleDb) noexcept
    {
        return g.plot.x + (db - kLevelTopDb + scaleDb) * g.plot.w / scaleDb;
    }

    // METERS (02 §6.3, §7.3, §8.8).
    enum class MeterBar : uint8_t { inL, inR, in, sc, gr, outL, outR, out };
    struct MeterGeom
    {
        struct Bar   { MeterBar what; Rect r; };
        struct Label { const char* text; float centreX; };
        Rect                 area;           // hit area: bars, labels and readouts
        LevelMap             level;
        int                  nBars;
        std::array<Bar, 5>   bars;
        float                labelY;         // kMicro ink52, centred
        int                  nLabels;
        std::array<Label, 4> labels;
        int                  nReadouts;      // max IN peak, max GR, max OUT peak since reset (kMicro, centred)
        std::array<Rect, 3>  readouts;
        Rect                 reset;          // one click resets every hold
        float                rmsWidth;       // RMS bar inside the peak fill: 2 px band, 4 px Characteristics
        Point                caption;        // "METERS" (Characteristics only; the band has none)
        bool                 hasCaption;
    };
    inline constexpr MeterGeom kBandMeters {
        { 764.0f, 122.0f, 156.0f, 228.0f }, kBandLevel, 5,
        { { { MeterBar::inL, { 783.0f, 140.0f, 6.0f, 192.0f } },  { MeterBar::inR, { 791.0f, 140.0f, 6.0f, 192.0f } },
            { MeterBar::gr, { 837.0f, 140.0f, 10.0f, 192.0f } },  { MeterBar::outL, { 887.0f, 140.0f, 6.0f, 192.0f } },
            { MeterBar::outR, { 895.0f, 140.0f, 6.0f, 192.0f } } } },
        340.0f, 3, { { { "IN", 790.0f }, { "GR", 842.0f }, { "OUT", 894.0f }, { nullptr, 0.0f } } },
        3, { { { 764.0f, 122.0f, 52.0f, 16.0f }, { 816.0f, 122.0f, 52.0f, 16.0f }, { 868.0f, 122.0f, 52.0f, 16.0f } } },
        { 764.0f, 122.0f, 156.0f, 16.0f }, 2.0f, { 0.0f, 0.0f }, false };
    inline constexpr MeterGeom kCharsMeters {
        { 732.0f, 122.0f, 64.0f, 278.0f }, kCharsLevel, 4,
        { { { MeterBar::in, { 734.0f, 140.0f, 8.0f, 240.0f } },  { MeterBar::sc, { 750.0f, 140.0f, 8.0f, 240.0f } },
            { MeterBar::gr, { 766.0f, 140.0f, 8.0f, 240.0f } },  { MeterBar::out, { 782.0f, 140.0f, 8.0f, 240.0f } },
            { MeterBar::out, { 0.0f, 0.0f, 0.0f, 0.0f } } } },
        381.0f, 4, { { { "IN", 736.0f }, { "SC", 752.5f }, { "GR", 769.0f }, { "OUT", 788.5f } } },   // S9 5a
        0, { { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } } },
        { 732.0f, 122.0f, 64.0f, 16.0f }, 4.0f, { 732.0f, 126.0f }, true };

    // READOUTS (02 §7.3): 18 rows at 13 px, kMicro; name ink52 left, value ink100 right-aligned.
    struct ReadoutsGeom
    {
        Rect  area;
        Point caption;                       // "READOUTS"
        int   rows;
        float rowPitch;
        float nameX;
        float valueRight;
    };
    inline constexpr ReadoutsGeom kReadouts { { 812.0f, 140.0f, 108.0f, 240.0f }, { 812.0f, 126.0f }, 18, 13.0f,
                                              812.0f, 920.0f };
    namespace readouts
    {
        // Rows 1–10; rows 11–18 are ModeDescriptor::internals[0..7] (name, unit and decimals from the descriptor).
        inline constexpr std::array<const char*, 10> kNames { "DET", "OVER", "TARGET", "APPLIED", "S2 GR", "EFF RATIO",
                                                              "ATK EFF", "REL EFF", "CREST", "PHASE" };
        inline constexpr int   kFirstInternalRow = 10;          // 0-based: row 11
        inline constexpr float kPairDb = 0.1f;                  // DET / APPLIED show L/R (M/S) pairs when the lanes
                                                                // differ by more than this and link < 1
    }

    // CONTROL PATH (02 §7.3): x-aligned with the Characteristics HISTORY (the same 210 columns).
    struct ControlPathGeom
    {
        Rect  plot;
        int   columns;
        float colWidth;
        Point caption;                       // "CONTROL PATH · GR"
        Rect  grLane;                        // 0 at the top, 0…S/2 dB over its height, hanging
        Rect  phaseLane;
        Rect  internalLane;                  // internal0, normalised lo…hi
        Rect  eventsLane;                    // four stripe rows: AUTO SLOW, RANGE, STAGE 2, FADE
        float eventRowPitch;
        float eventRowHeight;
        float eventLabelX;
        float timeLabelY;
        float timeLabelPitch;
    };
    inline constexpr ControlPathGeom kControlPath {
        { 40.0f, 422.0f, 420.0f, 160.0f }, 210, 2.0f, { 40.0f, 408.0f },
        { 40.0f, 422.0f, 420.0f, 80.0f }, { 40.0f, 506.0f, 420.0f, 3.0f }, { 40.0f, 514.0f, 420.0f, 30.0f },
        { 40.0f, 550.0f, 420.0f, 32.0f }, 8.0f, 5.0f, 44.0f, 586.0f, 84.0f };
    namespace controlPath
    {
        struct EventRow { const char* label; uint32_t bit; };  // HistoryColumn::bits
        inline constexpr std::array<EventRow, 4> kEvents { { { "AUTO SLOW", 1u << 2 }, { "RANGE", 1u << 3 },
                                                             { "STAGE 2", 1u << 4 }, { "FADE", 1u << 6 } } };
    }

    // STEP RESPONSE (02 §7.3): log time over [tMinS, tMaxS] from the step; 0 % of the steady-state GR at y0, 100 % at
    // y100 (hanging).
    enum class StepKind : uint8_t { attack, release };
    struct StepGeom
    {
        StepKind    kind;
        Rect        plot;
        Point       caption;
        const char* captionText;
        float       y0, y100;
        float       tMinS, tMaxS;
        float       labelY;
    };
    inline constexpr StepGeom kStepAttack  { StepKind::attack,  { 476.0f, 422.0f, 116.0f, 160.0f }, { 476.0f, 408.0f },
                                             "ATTACK",  438.0f, 566.0f, 5.0e-6f, 0.5f, 586.0f };
    inline constexpr StepGeom kStepRelease { StepKind::release, { 600.0f, 422.0f, 116.0f, 160.0f }, { 600.0f, 408.0f },
                                             "RELEASE", 438.0f, 566.0f, 1.0e-3f, 10.0f, 586.0f };

    struct AxisLabel { float value; const char* text; };       // a label at `value` on its plot's axis
    namespace step
    {
        inline constexpr std::array<AxisLabel, 3> kAttackLabels  { { { 1.0e-5f, ".01" }, { 1.0e-3f, "1" },
                                                                     { 1.0e-1f, "100 MS" } } };
        inline constexpr std::array<AxisLabel, 3> kReleaseLabels { { { 1.0e-3f, "1MS" }, { 1.0e-1f, ".1" },
                                                                     { 10.0f, "10S" } } };
    }

    // SIDECHAIN | COLOUR (02 §7.3): one area, two tab-pinned panes (UiState::scTab).
    inline constexpr Rect kScColour      { 732.0f, 422.0f, 188.0f, 160.0f };
    inline constexpr Rect kTabSidechain  { 732.0f, 404.0f, 62.0f, 16.0f };  // kCaption, text style, tab-pinned
    inline constexpr Rect kTabColour     { 800.0f, 404.0f, 44.0f, 16.0f };

    struct SidechainGeom
    {
        Rect  plot;
        float fMinHz, fMaxHz;                // log f
        float dbTop, dbBottom;               // linear dB
        int   points;                        // analysis::scResponse evaluations, log-spaced
    };
    inline constexpr SidechainGeom kSidechain { { 732.0f, 430.0f, 188.0f, 136.0f }, 20.0f, 20000.0f, 0.0f, -24.0f, 161 };
    namespace sidechain
    {
        inline constexpr std::array<AxisLabel, 4> kLabels { { { 20.0f, "20" }, { 200.0f, "200" }, { 2000.0f, "2K" },
                                                              { 20000.0f, "20K" } } };   // + the unit "HZ", at y 586
        inline constexpr float kLabelY = 586.0f;
    }

    struct ColourGeom
    {
        Rect  plot;                          // linear −1…+1 on both axes
        Rect  harmonics;                     // H2 H3 H4 H5 THD at harmonicsAmpDb
        int   segments;
        float harmonicsAmpDb;
    };
    inline constexpr ColourGeom kColour { { 732.0f, 430.0f, 136.0f, 136.0f }, { 876.0f, 430.0f, 44.0f, 136.0f }, 128, -6.0f };

    // ---- regions of the sub-views ---------------------------------------------------------------------------------------
    inline constexpr Rect kBand       { 40.0f, 122.0f, 880.0f, 232.0f };   // cells y 122 … axis labels y 340 (to 354)
    inline constexpr Rect kCharScreen { 40.0f, 122.0f, 880.0f, 478.0f };   // the middle region, incl. its cell row
    inline constexpr Rect kPresetStrip = header::kPresetStrip;

    // ---- band behaviour (02 §6.5) ---------------------------------------------------------------------------------------
    namespace band
    {
        inline constexpr float kStaleS          = 0.5f;          // LiveFeed staleness (or kUiLive clear)
        inline constexpr float kStaleDim        = 0.5f;          // retired by ADR-69 (UF1a): nothing dims when stale
        inline constexpr float kStaleDimS       = 0.4f;          // retired by ADR-69 (UF1a)
        inline constexpr float kGridDb          = 12.0f;         // grid hairlines every 12 dB (every 6 at S <= 24)
        inline constexpr float kGridDbFine      = 6.0f;
        inline constexpr float kZeroLabelDx     = 4.0f;          // "0 DBFS" at (plot.x + 4, y(0) − 12), kMicro ink32
        inline constexpr float kZeroLabelDy     = -12.0f;
        inline constexpr int   kStateLaneMaxRuns = 40;           // merged runs, <= 40 rrects
        inline constexpr float kLineHitPx       = 4.0f;          // threshold line drag hit ±4 px (UpDown cursor)
        inline constexpr float kTrailS          = 0.32f;         // OP_TRAIL: the last 320 ms ...
        inline constexpr float kTrailStepS      = 0.01f;         // ... sampled every 10 ms
        inline constexpr int   kCurveUniform    = 64;            // TRANSFER sampling: 64 uniform ...
        inline constexpr int   kCurveKnee       = 32;            // + 32 inside [T_in ± W/2]
        inline constexpr int   kCurveRange      = 16;            // + 16 around the range break
        inline constexpr int   kCurveCustom     = 120;           // custom/FB families: 120 uniform
        inline constexpr int   kWedgeColumns    = 48;            // GR_WEDGE: 48 AREA columns ...
        inline constexpr float kWedgeColumnW    = 4.0f;          // ... of 4 px
        inline constexpr float kNetCurveDb      = 0.05f;         // NET_CURVE drawn iff it differs by > 0.05 dB
        inline constexpr float kGhostHoldS      = 0.9f;          // the previous Mode's curve stays ink16 0.9 s
        inline constexpr float kCurveEaseS      = 0.16f;         // the curve eases old -> new over 160 ms
        inline constexpr float kThresholdHandle = 7.0f;          // 7×7 ring on unity at x(T_in)
        inline constexpr float kHandle          = 5.0f;          // knee, ratio, range: 5×5 rings
        inline constexpr float kRatioHandleDb   = 12.0f;         // ratio handle at x = min(T_in + 12, +3)
        inline constexpr float kRatioHandleMaxDb = 3.0f;
        inline constexpr float kOpDotR          = 3.5f;          // OP_DOT disc (hollow while kUiFading)
        inline constexpr float kTargetDotR      = 2.5f;          // TARGET_DOT ring, border 1
        inline constexpr float kNeedleW         = 2.0f;          // GR_NEEDLE
        inline constexpr float kHandleHoverInS  = 0.09f;         // handles fade in 90 ms, out 160 ms
        inline constexpr float kHandleHoverOutS = 0.16f;
        inline constexpr float kRelativeDragPx  = 240.0f;        // relative handle drags: 240 px per full track
        inline constexpr float kSnapHysteresisPx = 6.0f;         // stepped handles snap with 6 px hysteresis
        inline constexpr float kKneeTickH       = 4.0f;          // knee bracket end ticks 1×4
    }

    // ---- Characteristics behaviour (02 §7.1–§7.4) ---------------------------------------------------------------------
    namespace chars
    {
        inline constexpr float kScreenFadeTau  = 0.12f;          // ScreenFader: τ 0.12 s ...
        inline constexpr float kScreenFadeSnap = 1.0e-3f;        // ... snap at 1e−3
        inline constexpr float kPreviewMaxHz   = 20.0f;          // step responses <= 20 Hz during drags
        inline constexpr float kMarkerHitPx    = 4.0f;           // STEP spec markers: ±4 px hit (LeftRight cursor)
        inline constexpr float kStepMeasR      = 2.5f;           // STEP_MEAS: 5 px ring
        inline constexpr float kScHandleDb     = -3.0f;          // SC corner handle at (x(scHpfHz), y(−3 dB))
        inline constexpr float kScOffHz        = 20.0f;          // dragging left past 20 Hz = OFF
        inline constexpr float kPlotTitleHz    = 4.0f;           // a11y plot titles regenerated <= 4 Hz
        inline constexpr std::array<float, 3> kAttackStepsDb { 6.0f, 12.0f, 24.0f };   // over T_in (ink32/52/70)
        inline constexpr std::array<float, 2> kReleaseBurstsS { 0.05f, 2.0f };         // bursts before release
    }

    // ---- Mode browser (02 §8.6) ---------------------------------------------------------------------------------------
    namespace browser
    {
        inline constexpr float kOpenTau     = 0.12f;             // the overlay eases in τ 0.12 s
        inline constexpr int   kColumns     = 8;                 // 01 Group order: VCA FET OPTO VARI-MU DIODE MODERN LIMIT OTHER
        inline constexpr float kColumnX0    = 40.0f;             // x = 40 + 110·k
        inline constexpr float kColumnW     = 110.0f;
        inline constexpr float kHeadingY    = 72.0f;             // kCaption ink52, "FET  3"
        inline constexpr float kRowY0       = 92.0f;             // rows y 92 … 312 on a 20 px pitch
        inline constexpr float kRowPitch    = 20.0f;
        inline constexpr int   kRows        = 12;
        inline constexpr float kRowHitW     = 106.0f;            // row hit {colX, rowY − 4, 106, 20}
        inline constexpr float kRowHitDy    = -4.0f;
        inline constexpr float kCurrentBarDx = -6.0f;            // the current Mode: a 2×12 ink100 bar at x − 6
        inline constexpr float kCurrentBarW = 2.0f;
        inline constexpr float kCurrentBarH = 12.0f;
        inline constexpr float kPagerY      = 334.0f;            // ‹ 1/2 › when there are more than 8 columns
        inline constexpr int   kCapacity    = kColumns * kRows;  // 96 Modes before anything changes
        inline constexpr float kTypeAheadS  = 1.0f;              // type-ahead buffer

        constexpr float columnX(int k) noexcept { return kColumnX0 + kColumnW * static_cast<float>(k); }
        constexpr float rowY(int r) noexcept { return kRowY0 + kRowPitch * static_cast<float>(r); }
        constexpr Rect  rowHit(int k, int r) noexcept { return { columnX(k), rowY(r) + kRowHitDy, kRowHitW, kRowPitch }; }
    }

    // ---- landing and meters (02 §8.7, §8.8) ---------------------------------------------------------------------------
    namespace landing
    {
        inline constexpr float kCaretTau  = 0.09f;               // carets ease to a Mode switch's positions
        inline constexpr float kFlashS    = 0.6f;                // changed slots flash their label ink100 ...
        inline constexpr float kFlashTau  = 0.16f;               // ... then ease back
    }
    namespace meters
    {
        inline constexpr float kHoldS        = 1.5f;             // peak hold, then ...
        inline constexpr float kFallDbPerS   = 20.0f;            // ... falls at 20 dB/s (also when not live)
        inline constexpr float kReadoutPitch = 52.0f;
    }

    // ---- UF1a additions (S11; ADR-69, ADR-70): additive, no FZ4 declaration above changed ------------------------------
    // ADR-69 retires band::kStaleDim and band::kStaleDimS: staleness never dims anything (views/Telemetry.h). They stay
    // declared (FZ4 is additive only) and nothing reads them.
    namespace live
    {
        inline constexpr float kFadeS   = 0.4f;                  // OP_DOT, GR_NEEDLE, OP_TRAIL, TARGET_DOT fade out
                                                                 // where they were over 0.4 s when the feed stops
        inline constexpr float kActiveS = 0.5f;                  // full frame rate for 0.5 s after any input (hover,
                                                                 // drag, click, wheel, keys), then idle if nothing moves
    }
    namespace display
    {
        inline constexpr float kGrHoldS    = 1.0f;               // GAIN REDUCTION = the max GR over the last 1.0 s ...
        inline constexpr float kGrRefreshS = 0.25f;              // ... its text (and IN · OUT) refreshed every 0.25 s
                                                                 // of panel time (≈ 4 Hz), quantised in time
        // The live GR bar (ADR-70): signal, 2 px, from x 224 (the sub-readout's x) along the value's baseline (y 112),
        // at the band's px/dB (layout::kBandTransfer.level.pxPerDb(S): 4 px/dB at the default 48 dB scale), clipped to
        // kSubMaxW; its track a 1 px ink16 hairline under it; the held value (the readout) a 1×6 ink100 tick.
        inline constexpr Rect  kGrBar       { 224.0f, 110.0f, 148.0f, 2.0f };
        inline constexpr float kGrTrackY    = 111.0f;
        inline constexpr float kGrTickTop   = 107.0f;
        inline constexpr float kGrTickH     = 6.0f;
    }

    // ---- UF1b additions (S12; ADR-68, ADR-68a): additive, no FZ4 declaration above changed ------------------------------
    // The ZOOM cells beside THEME in the footer band. EditorHost (FunkGui v0.8.0) does the whole zoom: gpu/Editor.cpp passes
    // kZoomSteps and kDefaultZoomPercent to its EditorConfig, and the Footer draws one cell per step, so the two never
    // disagree. The panel stays 960 × 640 logical px at every zoom; nothing here scales.
    // Geometry (the display row's cell rule, above): each cell is w("100") in kCaption (17.6 px) + 14 = 32 px wide, 16 px
    // tall on the THEME cells' row (y 601), 4 px apart as the THEME cells are; the last one ends 12 px before GRAPHITE
    // (its label 30 px from GRAPHITE's). The caption "ZOOM" (kCaption ink52, 24.2 px) sits 4 px under the cells' top, as
    // the display row's captions do, and 8 px before the first cell. The spec line gives up the room: it is fitted to
    // kSpecLineW (x 40–594, 12 px before the caption) instead of footer::kSpecMaxW (740, retired: nothing reads it). 554 px
    // still holds the longest line that is not a slot's spec, the lookahead hint ("BRICKWALL WITHOUT LOOKAHEAD CAN
    // OVERSHOOT — SET LOOKAHEAD 5 MS ABOVE (+5 MS LATENCY)", 552.8 px); slot spec lines were already cut at 740.
    namespace footer
    {
        inline constexpr std::array<int, 4> kZoomSteps { 100, 125, 150, 175 };   // percent, ascending (ADR-68)
        inline constexpr int   kDefaultZoomPercent = 125;                        // a missing or unlisted preference
        inline constexpr Point kZoomCaption { 606.0f, 605.0f };                  // "ZOOM", kCaption ink52
        inline constexpr std::array<Rect, 4> kZoomCells { { { 638.0f, 601.0f, 32.0f, 16.0f },      // 100
                                                            { 674.0f, 601.0f, 32.0f, 16.0f },      // 125
                                                            { 710.0f, 601.0f, 32.0f, 16.0f },      // 150
                                                            { 746.0f, 601.0f, 32.0f, 16.0f } } };  // 175
        inline constexpr float kSpecLineW = 554.0f;                              // the spec line's fit width
    }

    // ---- UF2 additions (S12; ADR-72): additive, no FZ4 declaration above changed ------------------------------------
    // The band's HISTORY · VU switch and the GR VU meter (views/GrVuMeter.h). Only the band has them: the Characteristics
    // screen's HISTORY (kCharsHistory) and CONTROL PATH are unchanged.
    // - The switch replaces the "HISTORY" caption at kBandHistory.caption with two cells, HISTORY and VU, in the span
    //   cells' look (CellStyle::text) and hit rule (the display row's cell rule above: 16 px tall on the span cells' row,
    //   y 122; width = w(label) in kCaption + 14 rounded to an even number; 4 px apart). The HISTORY cell starts 7 px
    //   left of the plot, so its centred label keeps the old caption's x 40 (within 0.1 px) and stays aligned with the
    //   plot frame, as TRANSFER's caption is with its plot; Band::hit adds the cells to the band region for that 7 px.
    // - The choice is the machine-wide UiPreferences int "grView" (0 = HISTORY, the default; 1 = VU), read and written
    //   like historySpanTenths. While VU is shown the span cells, their "S" and the time labels are hidden (not dimmed),
    //   and the plot rectangle holds the meter instead of the traces; the state lane and the part of the threshold line
    //   outside the plot (into the TRANSFER handle) stay as they are, so nothing outside the plot rectangle, the caption
    //   row and the time-label row changes.
    // - The meter: one needle on a pivot centred in the plot, a thin arc scale (the rule and the ticks point outwards,
    //   the labels outside them) that is symmetric about the pivot: −20 dB at −kEndDeg, +3 dB at +kEndDeg from vertical.
    //   Deflection d = 10^((dB − 3) / 20) (the classic GR-on-a-VU law: linear gain, full scale = +3 dB, 0 dB at
    //   10^(−3/20) = 70.8 % of full scale) maps linearly to the angle, so d = 0 (infinite GR) sits 7.3° left of −20. The
    //   meter's box (the top label's cap top to the pivot's bottom) is centred in the plot within 0.5 px.
    // - Ballistics (IEC 60268-17's VU): a mass-spring needle, ζ = 0.81272 and ω0 = 13.5119 rad/s (f0 = 2.150 Hz), so a
    //   step reaches 99 % of its travel in 300 ms and overshoots by 1.25 %; integrated exactly (a zero-order hold per 1 ms
    //   HistoryColumn) over HISTORY's timeline (views/Telemetry.h), so it runs at audio time while the feed is fresh and
    //   falls back to rest at wall-clock time once it is stale (ADR-69), with the same law. It is drawn through a display
    //   clock kShowLagMs behind the newest audio (below), so host blocks do not make it step.
    namespace vu
    {
        inline constexpr int   kHistory = 0;                      // "grView" values
        inline constexpr int   kVu = 1;
        inline constexpr int   kDefaultView = kHistory;
        inline constexpr std::array<Rect, 2> kViewCells { { { 33.0f, 122.0f, 58.0f, 16.0f },      // HISTORY (43.8 px)
                                                            { 95.0f, 122.0f, 26.0f, 16.0f } } };   // VU (11.1 px)

        inline constexpr Point kPivot { 290.0f, 318.0f };         // the plot's centre x; the box centred in y
        inline constexpr float kPivotR  = 3.0f;                   // a disc, ink70
        inline constexpr float kScaleR  = 150.0f;                 // the arc: a 1 px ink32 rule
        inline constexpr float kRuleW   = 1.0f;
        inline constexpr float kTickLong  = 7.0f;                 // outwards from the arc, 1 px: labelled ink52, the
        inline constexpr float kTickShort = 4.0f;                 // +1…+3 region ink32; minor ticks short, ink32
        inline constexpr float kTickW   = 1.0f;
        inline constexpr float kLabelR  = 164.0f;                 // label centres, kMicro ink52, cap-centred
        inline constexpr float kNeedleR = 156.0f;                 // the needle: pivot to tip, 1.5 px signal
        inline constexpr float kNeedleW = 1.5f;
        inline constexpr float kLegendDy = 96.0f;                 // "GR · VU" cap-centred this far above the pivot
        inline constexpr float kEndDeg  = 48.0f;                  // the scale's ends, degrees from vertical
        inline constexpr float kLowDb   = -20.0f;                 // ... at these readings
        inline constexpr float kHighDb  = 3.0f;
        inline constexpr int   kArcSegments = 96;                 // the arc as a polyline

        inline constexpr std::array<AxisLabel, 8> kLabels { { { 0.0f, "0" },  { -1.0f, "\xE2\x88\x92" "1" },
                                                              { -2.0f, "\xE2\x88\x92" "2" }, { -3.0f, "\xE2\x88\x92" "3" },
                                                              { -5.0f, "\xE2\x88\x92" "5" }, { -7.0f, "\xE2\x88\x92" "7" },
                                                              { -10.0f, "\xE2\x88\x92" "10" },
                                                              { -20.0f, "\xE2\x88\x92" "20" } } };
        inline constexpr std::array<float, 3> kOverDb  { 1.0f, 2.0f, 3.0f };                  // long ticks, no labels
        inline constexpr std::array<float, 5> kMinorDb { -15.0f, -9.0f, -8.0f, -6.0f, -4.0f }; // short ticks
        inline constexpr const char* kLegend = "GR \xC2\xB7 VU";   // kMicro ink32

        inline constexpr double kZeta  = 0.8127170;               // 1.25 % overshoot
        inline constexpr double kOmega = 13.511913;               // rad/s: 99 % of a step at 0.300 s
        inline constexpr int    kCatchUpMs = 1000;                // a meter ticked after a pause integrates at most the
                                                                  // last second of its timeline (>> the 0.3 s settle)
        inline constexpr float  kValueS = 0.1f;                   // its a11y value regenerated at <= 10 Hz (02 §9.6)

        // The needle is integrated up to the timeline's head, but drawn on a display clock that runs at the panel's own
        // rate kShowLagMs of audio behind it, so audio that arrives in host blocks (Logic's 1024-sample process buffer
        // is 23 ms) still moves it smoothly at the frame rate instead of in block steps. While fresh a slow pull (kPullS)
        // keeps that lag, so its rate never departs from real time by more than a few per cent; over a stale span the
        // clock simply runs at real time up to the head (the fall to rest is drawn exactly); more than kMaxLagMs
        // behind (a burst, a pause) it skips forward.
        inline constexpr double kShowLagMs = 40.0;
        inline constexpr double kMaxLagMs  = 250.0;
        inline constexpr double kPullS     = 1.0;

        // ---- v1.1 hardware faces (ADR-76; views/MeterFaces.h) ----------------------------------------------------------
        // An analog plate: a bezel (radius 12) with a window inset 8 (radius 6); the ring plate adds a bright ring
        // kHwRingInset inside the bezel. The scale is the panel face's arc (pivot, kScaleR, ±kEndDeg); ticks point
        // inwards from it (long kHwTickLong, short kHwTickShort) and labels sit inside at kHwLabelR. The needle runs
        // from the pivot to kHwNeedleR and its base hides under a shroud at the window's bottom, as on a real meter.
        inline constexpr Rect  kHwBezel     { 90.0f, 144.0f, 400.0f, 184.0f };   // centred in the plot (4 px margins)
        inline constexpr float kHwBezelR    = 12.0f;
        inline constexpr float kHwEdgeMix   = 0.14f;               // the bezel's 1 px edge: its colour towards white
        inline constexpr float kHwWindowInset = 8.0f;
        inline constexpr float kHwWindowR   = 6.0f;
        inline constexpr float kHwRingInset = 4.0f;
        inline constexpr float kHwRingW     = 3.0f;
        inline constexpr float kHwTickLong  = 10.0f;
        inline constexpr float kHwTickShort = 5.0f;
        inline constexpr float kHwTickW     = 1.2f;
        inline constexpr float kHwArcW      = 1.2f;
        inline constexpr float kHwZoneR     = 146.0f;              // the red zone: a band inside the arc
        inline constexpr float kHwZoneW     = 4.0f;
        inline constexpr float kHwLabelR    = 128.0f;
        inline constexpr float kHwNeedleR   = 154.0f;
        inline constexpr float kHwNeedleW   = 1.6f;
        inline constexpr float kHwLegendDy  = 80.0f;               // the legend's cap centre above the pivot
        inline constexpr float kHwLegend2Dy = 60.0f;               // the small legend's
        inline constexpr Rect  kHwShroud    { 236.0f, 296.0f, 108.0f, 24.0f };   // top corners rounded 12
        inline constexpr Point kHwLamp      { 112.0f, 166.0f };    // the Mode colour's lamp (ADR-75), radius 3
        inline constexpr float kHwLampR     = 3.0f;
        inline constexpr Rect  kHwGlow      { 176.0f, 200.0f, 228.0f, 60.0f };   // the backlit plate's lamp glow; with
        inline constexpr float kHwGlowSoft  = 40.0f;               // its softness it stays inside the window
        inline constexpr float kHwGlowAlpha = 0.55f;

        // The LED ladder: a bezel with a window inset 8, 24 segments of 1 dB (kLedPitch apart, left = 0 dB GR), the
        // legend over them and the dB labels under them at the segment edges.
        inline constexpr Rect  kLedBezel    { 100.0f, 184.0f, 380.0f, 104.0f };   // centred in the plot
        inline constexpr float kLedSegW     = 11.0f;
        inline constexpr float kLedPitch    = 14.0f;
        inline constexpr float kLedSegY     = 216.0f;
        inline constexpr float kLedSegH     = 28.0f;
        inline constexpr float kLedSegR     = 2.0f;
        inline constexpr float kLedLeft     = 290.0f - 0.5f * (24.0f * 14.0f - 3.0f);   // 123.5: the ladder centred
        inline constexpr float kLedLegendY  = 202.0f;              // cap centres
        inline constexpr float kLedLabelY   = 258.0f;
        inline constexpr Point kLedLamp     { 118.0f, 202.0f };

        // ---- v1.2 plate detail (ADR-81; METER_DETAIL, drawn over the ADR-76 faces, never read by the probes' needle,
        // tick, label or segment finders) -------------------------------------------------------------------------------
        // Bezel screws: one in each corner of the bezel rim (inset so the head stays inside the corner's radius and off
        // the window), a slot at a different angle on each.
        inline constexpr float kScrewInset  = 5.5f;
        inline constexpr float kScrewR      = 2.4f;
        // The window's recess: three stacked translucent bands under its top edge (a stepped shadow), and the glass: a
        // faint light wedge from the top-left corner (two layers, brighter near the corner).
        inline constexpr std::array<float, 3> kRecessH { 9.0f, 5.0f, 2.0f };
        inline constexpr float kRecessAlpha = 0.05f;
        inline constexpr float kSheenAlphaLight = 0.10f;           // on a light face: the corner's total, over ...
        inline constexpr float kSheenAlphaDark  = 0.06f;           // ... a dark face's
        inline constexpr int   kSheenLayers = 5;                   // nested wedges, each 1/kSheenLayers of it: it fades
        inline constexpr float kSheenW = 0.66f, kSheenH = 0.74f;   // the largest wedge (fractions of the window) ...
        inline constexpr float kSheenStep = 0.12f;                 // ... and each smaller one's
        // The needle: a soft shadow on the face (offset down-right) and a thicker base above the shroud.
        inline constexpr Point kNeedleShadow { 2.5f, 3.0f };
        inline constexpr float kNeedleShadowAlpha = 0.22f;
        inline constexpr float kNeedleShadowSoft  = 1.2f;
        inline constexpr float kNeedleShadowR0 = 30.0f;            // from above the shroud (its top is 22 px up)
        inline constexpr float kNeedleBaseR = 38.0f;               // pivot to the base's end
        inline constexpr float kNeedleBaseW = 2.4f;
        // The shroud: a highlight along its top and a zero-adjust screw.
        inline constexpr float kZeroScrewR  = 3.2f;
        inline constexpr float kZeroScrewDy = 13.0f;               // below the shroud's top
        // The Mode lamp as a jewel: a halo of its light, a metal rim and a specular point.
        inline constexpr float kLampHalo    = 3.0f;
        inline constexpr float kLampHaloAlpha = 0.30f;
        inline constexpr float kLampRimW    = 1.0f;
        // A VU face's secondary scale, 0–100 % of 0 VU's voltage (the classic lower scale): ticks inwards from
        // kPctTickR at 20 … 100 % (minor at 10 … 90 %), labels at kPctLabelR, "%" at the right end.
        inline constexpr float kPctTickR    = 113.0f;
        inline constexpr float kPctTickLong = 5.0f;
        inline constexpr float kPctTickShort = 2.5f;
        inline constexpr float kPctLabelR   = 101.0f;
        inline constexpr float kPctArcW     = 0.8f;                // the scale's own arc at kPctTickR, 10 % … the end
        // A GR scale's fine ruler: a short tick at every whole dB its marks leave out.
        inline constexpr float kFineTick    = 3.0f;
        // The LED ladder: a lit segment's bloom and every segment's lens highlight (the top of the segment).
        inline constexpr float kLedHalo     = 3.0f;
        inline constexpr float kLedHaloSoft = 5.0f;
        inline constexpr float kLedHaloAlpha = 0.30f;
        inline constexpr float kLedLensFrac = 0.32f;
        inline constexpr float kLedLensAlphaLit = 0.30f;
        inline constexpr float kLedLensAlphaDim = 0.06f;
        inline constexpr float kLedTickY    = 247.0f;              // scale ticks between the ladder and its labels
        inline constexpr float kLedTickH    = 4.0f;
    }

    // ---- v1.2 (ADR-85): the SETTINGS overlay and its gear ----------------------------------------------------------------
    // The gear right of the wordmark opens it (the footer has no room: the ZOOM spec fills its line). The overlay covers
    // the region between the header and the footer, over a ground grown 8 px left and right and 4 px up and down as the
    // browsers' is; a click outside it (the header, the footer) closes it, as a browser's does.
    namespace settings
    {
        inline constexpr Rect  kGear { 196.0f, 12.0f, 24.0f, 24.0f };   // hit; drawn centred, kGearR
        inline constexpr float kGearR = 7.0f;                    // teeth tips
        inline constexpr float kGearBodyR = 5.0f;
        inline constexpr float kGearHoleR = 2.0f;
        inline constexpr int   kGearTeeth = 8;
        inline constexpr float kGearToothW = 2.4f;

        inline constexpr Rect  kArea { 40.0f, 64.0f, 880.0f, 526.0f };  // y 64–590
        inline constexpr Rect  kGround { kArea.x - 8.0f, kArea.y - 4.0f, kArea.w + 16.0f, kArea.h + 8.0f };
        inline constexpr float kHeadingY = 72.0f;                // kCaption: AUDIO · NEW INSTANCES · DIAGNOSTICS
        // The left column: a caption at x 40, its cells from kCellsX (the display row's cell look and width rule), the
        // tables and notes under them in kMicro.
        inline constexpr float kLabelX = 40.0f;
        inline constexpr float kCellsX = 184.0f;
        inline constexpr float kCellH = 16.0f;
        inline constexpr float kCellGap = 4.0f;
        inline constexpr float kCellPad = 14.0f;                 // a cell is w(label) + 14, rounded up to an even px
        inline constexpr float kQualityY = 92.0f;                // cell tops
        inline constexpr float kTableY0 = 116.0f;                // QUALITY's table: OVERSAMPLING, FILTER, LATENCY, RUNS AT
        inline constexpr float kTablePitch = 14.0f;
        inline constexpr float kBudgetY = 184.0f;
        inline constexpr float kBudgetTableY = 208.0f;           // LATENCY at the current rate
        inline constexpr float kKeyY = 238.0f;
        inline constexpr float kKeyNoteY = 262.0f;
        inline constexpr float kLatencyY = 292.0f;               // the total, kLabel
        inline constexpr float kLatencyNoteY = 310.0f;
        inline constexpr float kRuleY = 338.0f;                  // a hairline over NEW INSTANCES
        inline constexpr float kNewHeadingY = 350.0f;
        inline constexpr float kNewQualityY = 372.0f;
        inline constexpr float kNewBudgetY = 396.0f;
        inline constexpr float kNewNoteY = 422.0f;
        inline constexpr float kColumnRight = 460.0f;            // the left column's notes are fitted to x 460
        // The divider, then DIAGNOSTICS: a key (kMicro ink32) and its value (kMicro ink70) per row.
        inline constexpr float kDividerX = 480.0f;
        inline constexpr float kDiagKeyX = 500.0f;
        inline constexpr float kDiagValueX = 604.0f;
        inline constexpr float kDiagY0 = 96.0f;
        inline constexpr float kDiagPitch = 18.0f;
        inline constexpr float kRefreshS = 0.25f;                // the live values' text, every 0.25 s of panel time
        // COPY REPORT, a text cell right-aligned to the content edge, and its note left of it.
        inline constexpr float kCopyY = 566.0f;
        inline constexpr double kCopiedS = 2.0;                  // "COPIED" stays this long
    }
}
