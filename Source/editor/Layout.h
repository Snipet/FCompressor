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

    // ---- footer (02 §6.3, §6.6) ---------------------------------------------------------------------------------------
    namespace footer
    {
        inline constexpr Point kSpec { 40.0f, 604.0f };         // spec line, kLabel ink32, fitEllipsis to kSpecMaxW
        inline constexpr float kSpecMaxW = 740.0f;
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
}
