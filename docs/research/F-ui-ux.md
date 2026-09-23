# F: Characteristics screen and shared UI (FCompressor UX proposal)

Status: proposal, no code. Written 2026-09-22, based on:
- `docs/research/A-gui-stack.md` (A), the map of HR's GUI stack
- `B-plugin-build.md` (B) §7.4–7.5, the parameter model and telemetry
- `C-verification.md` (C) §3.3 and §5, the Panel/recorder split and the Mode contract
- HR sources, read-only. **Line numbers in `Source/BgfxEditor.cpp` are from 20:30 on 2026-09-22.** The file had 1987 lines then and is being edited, so it no longer matches A's 1968-line snapshot. Treat the line numbers as approximate.
- Vendor manuals and pages listed in §11.

HR = `/Users/seanfunk/audio/plugins/HardwareReverb`. "Mode" = one compressor type or style, as defined by `ModeSpec` (C §5.1).

---

## 0. Decisions in one screen

1. **One fixed panel of 960×640 logical px** (HR is 880×520). The extra 80×120 px goes to a 240 px characteristics band and a meter column. Every rule in HR's layout is kept: 40 px margin, fixed axes, no disclosure, no resize.
2. **The characteristics screen is a permanent band in the middle of the panel.** It is not a separate page. The band holds three regions that share one dB scale:
   - **HISTORY** (500×240): scrolls, time runs leftward into "now".
   - **TRANSFER** (240×240, square): static curve, knee, handles, live operating point.
   - **METERS** (96×240): IN, GR, OUT.

   **One dB is the same number of pixels everywhere in the band:** 5 px/dB at the default 48 dB scale. This has three consequences:
   - The history's threshold line, extended right, hits the threshold handle on the curve.
   - The newest output sample in the history sits at the height of the operating dot.
   - The GR needle on the curve, the GR line in the history and the GR meter all measure the same GR in the same pixels.
3. **Views switch the way HR's band does** (dwell 0.9 s, crossfade τ 0.18 s, hover never switches: `BgfxEditor.cpp:996-1031`). A tab row adds a *pinned* view per region, which is where the band returns after the dwell. Region H views: HISTORY, TIME (step response), SIDECHAIN, INTERNALS. Region T views: CURVE, COLOUR.
4. **Every Mode shows the same 15 slots, 6 attached latch words, the Mode latch, DELTA and BYPASS, all in the same places.** A Mode changes only how each slot *renders*, through a per-param `SlotSpec` with one of five states: **live, stepped, locked, derived, n/a**. A remap only changes the label and units. The panel never re-lays out.
5. **Stepped = the rule slider with detent cells.** The track is split into n equal cells. A tick sits at each cell centre, and the cell labels go on the sub-readout line when they fit. Dragging, the wheel and the arrows move one detent at a time, with hysteresis. Clicking a label jumps to it. This covers the 2-4-10 ratio, 4-8-12-20-ALL, time-constant 1–6, and so on, with no second widget and no layout change.
6. **Locked** shows a dotted track, no caret, the value in ink32 and a `FIXED` tag. **Derived** shows a hollow caret that moves with its driver and an `= RATIO`/`AUTO` tag. **N/A** shows the label in ink16, the value as `–` and no track. In every case **the reason goes on the footer spec line on hover or focus.** HR has no tooltips: the spec line replaces them (`BgfxEditor.cpp:1575-1605`).
7. **Remapped:** the slot takes the Mode's own name (THRESHOLD→INPUT, →PEAK RED.). The track always increases the *labelled* quantity to the right, so it can run opposite to the host parameter. The primary sub-readout prints the universal equivalent (`THRESHOLD −28 DB`).
8. **Mode selector:** a header latch `‹ FET-76 ›` with its group caption. Clicking it opens a **Mode browser drawn on the canvas** over the display row and band, with the controls still visible so the constraints are seen landing. Columns are grouped by topology (VCA · FET · OPTO · VARI-MU · DIODE · MODERN · …), 15 rows per column, 90 Modes before it needs to scroll.
9. **New canvas pieces needed:**
   - `KIND_AREA`, from A §2.6, plus a stroke-edge flag. It gives filled history, the GR wedge, and seam-free line plots that fade without beads.
   - A live/tag field per primitive, for C's fingerprint and G2/G3 probes.
   - `polyline`/`disc`/`areaStrip` wrappers.
   - A transient VB of `32<<20`.
   - The glyph µ (U+00B5).

   A typical frame is ≈1.4K primitives and the worst case ≈1.9K.
10. **Data:**
    - Seqlock `UiFrame`: 38 words, per block. Includes `curveXDb`, `grTargetDb`, `grAppliedDb`, the phase bits and `internals[8]`.
    - SPSC `HistoryRing`: 1 kHz points of 32 B, 4096 deep.
    - Three pure `ModeSpec` UI hooks: `presentation()`, `staticGainDb()`, `renderStepResponse()`.
    - A UI-side history store at 5 ms resolution, 4096 entries (20.5 s).
11. **Accessibility:**
    - A stepped slot is a slider in **index space**: range 0..n−1, interval 1. JUCE's macOS increment is `current + interval` (`juce_AccessibilitySharedCode_mac.mm:146-160`), so VoiceOver moves exactly one detent.
    - Locked and N/A items call `setEnabled(false)`, which maps to `isAccessibilityEnabled`, and `setHelpText(reason)`, which maps to `accessibilityHelp`.
    - Tab walks *every* operable item. This fixes HR's gap, where Tab walks only `controls_` (`:1880-1893`).

---

## 1. What the best compressors do: precedents and what to take from them

| Product | What its UI does (sourced) | Take for FCompressor | Avoid |
|---|---|---|---|
| **FabFilter Pro-C 3** | 14 styles in three groups (Modern: Clean, Versatile, Smooth, Punch, Upward, TTM; Classic: Op-El, Vari-Mu, Classic, Opto; Utility: Vocal, Mastering, Bus, Pumping), **all on one panel**. The knee display is an optional square overlay on the scrolling level display: the curve turns green to show the current input level. The level display shows input dark grey, output light grey with a light stroke, and GR as a red line. **Knee display, level display and meters share one meter scale** (9 dB to 90 dB selectable, remembered for new instances). A circular side-chain level meter surrounds the Threshold knob. Knee 0–72 dB, attack 0.005–250 ms, lookahead ≤20 ms. Style changes a knob's *meaning* without changing the panel: Vari-Mu's Ratio becomes tube drive, TTM's Threshold becomes a target level and Knee blends two stages, and Vocal sets knee and ratio automatically | The whole premise: many engines, one panel, grouped style list. The shared dB scale across curve, history and meters. The live detector level shown on the threshold control. Direct-edit knee overlay placed beside the history | The knee display as a hideable overlay. Ours is permanent (HR rule 10) |
| **DMG Compassion** | The main graph scrolls the input waveform between two white threshold "rails". GR bands at top and bottom are **green during attack and red during release**. Dragging the waveform sets Threshold and dragging the GR bands sets Ratio. Scrolling **stops on mouse-over**. There is a separate draggable Knee graph and a **Response graph that sketches the attack/release curve shape**. The Detector graph is an X-Y pad (circle handle = sum, square = peak). The Advanced section expands per module. Curve Law, AR coupling and AR order are exposed | **Envelope phase made visible** (our state lane). The **step-response preview** (our TIME view). Drag on the graph writes the parameter | Colour-coding the phase with red/green (our colour budget is two tokens). Pausing on hover (we pause on press-and-hold) |
| **Klanghelm MJUC** | Three models (MK1/MK2/MK3), each with **a different faceplate**. MK1 has a 6-position TIMING switch: 1–4 fixed, 5–6 program-dependent, one switch covering attack and release. MK2/MK3 have Ratio plus program-dependent Attack (≈0.8 or 0.4–35 ms) and Recovery (≈20 ms–3.6 s). VU mode In/Out/GR/OUT−IN, cycled by right-click | The idea of **one detented control standing for attack and release together** (our remap: Attack→TIME, Release→derived). The OUT−IN meter mode | **Per-model faceplates.** They are exactly what the brief forbids |
| **Klanghelm DC8C 2** | Easy mode with 4 styles, which are really 4 separate compressors, and Expert mode. In LIMITING mode the manual lists controls that **have no function** (RMS, SHAPE, FB MIX, ATTACK, PRE ATT, ATT DEP, RANGE, MIX, SC filter). Curve shapes SOFT/NOSE/SPIKE, FF↔FB mix, program-dependency ± on attack and release, GR smoothing, RMS time 0 = peak | A Mode or state that disables controls is normal, so the UI needs a first-class **n/a** state with a reason. The DETECT slot as an RMS window where 0 = peak | Silently inert controls. DC8C documents the inert set only in the manual; ours says so on hover |
| **TDR Kotelnikov** | Continuous ratio 1.1:1–7:1, a Peak Crest control, separate Release Peak and Release RMS. The GR meter toggles GR / Total Gain by clicking its title, and its range is set by hover plus buttons. Delta listen. Equal-loudness bypass | DELTA latch. The meter range as a user choice (a fixed scale, not auto) | An auto-expanding meter scale. It would break HR rule 11 (fixed axes) |
| **TDR Molot GE** | FF/FB modes with **different knee shapes per mode**. The VU meter toggles GR / Total gain, and its range is 2–32 dB via right-click or the wheel. **Controls that do not apply are drawn "inactive"** (Saturate in Live/Eco quality) | Knee as a Mode-owned curve family (derived/locked knee). Precedent for the inactive rendering | – |
| **Cytomic The Glue** | Stepped hardware values: Ratio 2/4/10; Attack 0.01, 0.1, 0.3, 1, 3, 10, 30 ms; Release 0.1, 0.2, 0.4, 0.6, 0.8, 1.2 s, Auto. **Knee is linked to Ratio** (2 soft, 4 medium, 10 hard). Range 0 to −82 dB. **Double-click the outer ring to snap to a notched value.** The needle shows roughly RMS compression and a click adds a peak needle. Auto release = slow baseline plus fast transient stage | Knee **derived** from Ratio. The optional **soft detent** (magnetic notches on a continuous control). The **two-stage auto release** shown as two traces | – |
| **Sonnox Oxford Dynamics** | Four sections (Comp/Lim/Gate/Exp) with ACCESS buttons. **A live transfer-function graph is permanently visible.** Per-section GR contribution meters. Timing laws NORMAL/CLASSIC/LINEAR: in LINEAR the time readout is rescaled to "time for 10 dB", so the units on a control change with the law. The ACCESS page can follow the last-touched control | The permanent transfer graph. **Readout units that change with the Mode's time law** (our formatter per Mode). A view that follows the touched control (HR already does this) | Paging controls behind ACCESS buttons |
| **ToneBoosters Compressor 4** | Five modes (Punch, Complex, Smooth, Warm, Clean) defined by lookahead and detector (Hilbert or RMS, multiband). Tabs Levels / Side chain / Zones. In Levels, input is a filled area and gain is a curve over time, with a Scroll button (continuous or page-fill), auto vertical zoom, and a Curve toggle. **The horizontal threshold indicator is draggable.** "Slider assist" shows the GR currently applied on the Range slider | **Drag the threshold line in the history.** **Live GR drawn on the RANGE track.** A tab row for band views | Auto-zoom (auto-scaling) |
| **Weiss DS1-MK3 (Softube)** | Screen modes Waveform (GR, RMS and peak over time), Frequency and Knee. The Knee range is selectable. Meters are rescaled by dragging. Ratio runs from 1:5 (expansion) to 1000:1. Soft knee 0..1, where 1 = a curve from 0 dBFS down to twice the threshold | Several views of one screen area. The curve must handle expansion and 1000:1 | – |
| **UA 1176 collection** | **Input sets the threshold as well as the level into the unit.** Ratio buttons 4/8/12/20 plus multi-button and "All". Attack 20–800 µs and release 50 ms–1.1 s, where **turning up the knob shortens the time**. The AE adds 2:1 and a fixed 10 ms "Slo" attack. Meter GR / +8 / +4 / Off | The canonical **remap** (threshold → INPUT). Detents with a non-numeric label (ALL). A dial-number sub-readout | Reversed knob direction. Our tracks always increase the *labelled* quantity to the right |
| **SSL G bus (UA/Waves), Cytomic Glue** | Attack 0.1, 0.3, 1, 3, 10, 30 ms; release 0.1, 0.3, 0.6, 1.2 s, Auto; ratio 2/4/10; continuous threshold (±15 dB on the UA version, per a search summary) | Detent sets of 3–7 with short labels. Auto release as a latch (§3.5) | – |
| **Fairchild 670 (via UA/NI docs)** | Time constant 1–6: release ≈0.3, 0.8, 2, 5 s for 1–4, with 5–6 program-dependent; attack ≈0.2–0.8 ms (search summary) | The TIME remap (§3.4) | – |
| **LA-2A (UA)** | Peak Reduction, Gain and a Compress/Limit switch. **No attack or release controls:** attack ≈10 ms, with a program-dependent multi-stage release | Locked Attack, derived Release, a 2-detent ratio | – |
| **Arturia Comp FET-76** | 20–800 µs and 50 ms–1.1 s, all-buttons. The advanced panel adds a side-chain EQ, M/S and a Range control where 0 dB = distortion only. VU calibration −18/−12/−8 dBFS (search summary) | Vintage Modes may keep "modern add-ons" live (Range, Mix, SC HPF). The Mode decides | Advanced panels (disclosure) |
| **NI Supercharger GT** | Character Fat/Warm/Bright and saturation Mild/Moderate/Hot. GR and output meters (search summary) | A Mode-defined **VOICE** detent slot (§3.1) | – |
| **Kilohearts Compressor** | One display: input level, threshold and current attenuation. Mode RMS/Peak | Minimalism: the band's HISTORY already covers this | – |
| **Airwindows** | No custom GUI; the host's generic sliders (general knowledge, not fetched) | Nothing to take for the UI. It is a reminder that host lanes are the real UI for automation, so the Mode-aware `valueToText` from B §7.4 matters | – |

**Three lessons that decide the design:**
- Pro-C 3 proves that a many-engine compressor can keep one panel if it lets a Mode change a control's *meaning* and *availability*, and says so.
- MJUC and DC8C show the two failure modes: a different faceplate per model, and inert controls documented only in the manual.
- Compassion and TB prove that dragging directly on the graphs and showing the envelope's internals is what makes the "character" of a compressor legible.

---

## 2. Inherited rules (HR) that constrain every choice

| Rule | Where | Consequence here |
|---|---|---|
| One fixed window. All parameters visible. No disclosure. No resize | `BgfxEditor.h:17-18, 51-52`; A §5.2 #10 | The characteristics screen is a band, not a page (§4.1). The Mode browser is an overlay over the band only, like HR's preset browser (`PresetPanel.h:14-18`) |
| Fixed axes that never auto-scale | `BgfxEditor.cpp:29-31`; README "The Rank" | dB scale and history span are user choices from fixed sets (12/24/48/72 dB; 2.5/5/10/20 s), never automatic |
| Depth is ink only: ink100/70/52/32/16. No gradients, shadows or bevels | `gui/Theme.h:7-10` | Locked/derived/n/a are expressed only by ink level, track style and a text tag |
| Accent = the control under the hand | `BgfxEditor.cpp:1103-1140` comments; A §5.2 #3 | Accent: slot under the hand, the curve it shapes, the dragged handle, the focus ring. **Not** the current Mode or active detents at rest |
| Exactly one special-job token (`ice` = Freeze) | `Theme.h:21` | Rename to `signal` (A §6.4.6). **Job: live gain reduction.** Used for the GR line, GR meter, GR needle on the curve and the big GR number |
| Displays are truthful, and values are absent rather than faked | README 350-362; `:1460-1461` | Curves come from the Mode's own `staticGainDb`. With no audio, the operating dot and trail are *not drawn* and the history holds and dims |
| Motion: 90 ms in, 160 ms out, no overshoot, big jumps snap | `:917-928` (A §3.18) | A Mode switch *eases* carets and curves (it is a meaningful event). Preset recall snaps (HR) |
| No tooltips; a persistent footer spec line | `:1575-1605` | Reasons for locked/derived/n/a and remap aliases live on the spec line |
| Hover never switches views | `:1002-1005` | The TRANSFER handles appear on hover (chrome only). The band's *view* changes only on drag, wheel, key or tab click |
| Wheel requires a hit-test | A §3.4 | Nothing in the band writes on a wheel event unless the pointer is on a handle or slot |
| The 10 px type floor | `gui/TypeScale.h:3-6` | Detent labels are kMicro (10 px), shown only when they fit (§3.3) |

---

## 3. Main panel: the shared UI for all Modes

### 3.1 Universal slot set (UI view; reconcile with B §7.4 param IDs)

Fifteen slots, identical in every Mode. The widths come from `textWidth` at the HR type scale: JetBrains Mono advance 0.4545×px, plus tracking.

| Row | Slot (universal label) | Plain range (superset, host param) | Bipolar | Attached word (latch param) | Primary sub-readout (live, absent when not live) |
|---|---|---|---|---|---|
| P1 | THRESHOLD | −60..+12 dBFS | no | – | `DET −14.2`: `curveXDb` (Pro-C's SC ring). Also a detector tick under the track |
| P2 | RATIO | 1..∞ (∞ at top, B §7.4.1). Expansion <1 if ever needed | no | – | `EFF 2.7:1` = 1/(d out/d in) of `staticGainDb` at the operating point. Static text `−7.5 DB PER 10 DB OVER` when not live |
| P3 | ATTACK | 0.005 ms..250 ms | no | – | Mode dial (`DIAL 4.2`) or `EFF 0.8 MS` when program-dependent |
| P4 | RELEASE | 5 ms..5 s | no | **AUTO** (`autoRelease`) | `EFF 340 MS` (live effective release) |
| P5 | MAKEUP | −24..+24 dB | yes | **AUTO** (`autoMakeup`) | `AUTO +4.1` when auto is on |
| P6 | MIX | 0..100 % | no | – | `DRY −6.0 DB` |
| S1 | KNEE | 0..36 dB | no | – | (secondary slots have no sub line) |
| S2 | RANGE | 0..60 dB (60 = off) | no | – | GR-used bar on the track (TB's "slider assist"), in `signal` |
| S3 | HOLD | 0..500 ms | no | – | |
| S4 | LOOKAHEAD | 0..10 ms (latency fixed at max, B §7.4.5) | no | – | |
| S5 | DETECT | RMS window 0 (PEAK)..300 ms | no | **EXT** (`scExternal`) | |
| S6 | SC HPF | OFF, 20..500 Hz | no | **LISTEN** (`scListen`) | |
| S7 | LINK | 0..100 % | no | – | |
| S8 | DRIVE | 0..100 % (colour stage) | no | – | |
| S9 | VOICE | Mode-defined stepped, ≤4 detents | no | – | |

The rest of the panel:
- Header: **MODE** latch (`mode`, int 0..63, non-automatable, B §7.4.3).
- Display row: **DELTA** (`delta`) and **BYPASS** (`bypass`) latches.

**VOICE** is the escape valve for Mode-specific switches (1176 revision, opto HF emphasis, vari-mu L/R or M/S, colour type), without a per-Mode faceplate. Its detent labels come from the Mode. It is n/a in Modes without variants.

### 3.2 Per-Mode presentation model (`SlotSpec`)

This extends C §5.1's `Constraint`. It is a **pure function of (mode, param snapshot)**, so it can depend on other params. Examples: Release becomes *derived* while AUTO is on in some Modes, and Range becomes *n/a* while Ratio = LIMIT (the DC8C-style dependency). The editor computes it on the message thread once per frame, keyed by snapshot hash. The DSP, the host's `valueToText`, the a11y model and C's probes use the same function.

```cpp
namespace fcmp {
enum class SlotState : uint8_t { live, stepped, locked, derived, na };
struct Detent { float plain; const char* label; const char* spoken; };   // "ALL" / "all buttons"
struct SlotSpec {
    SlotState state = SlotState::live;
    const char* label   = nullptr;   // Mode term; nullptr -> universal label ("INPUT", "PEAK RED.", "TIME", "OUTPUT")
    const char* aka     = nullptr;   // universal name when remapped ("THRESHOLD") -> footer + a11y description
    const char* tag     = nullptr;   // label-row tag for locked/derived: "FIXED", "= RATIO", "= TIME", "AUTO"
    const char* reason  = nullptr;   // one line for footer + a11y help (required for locked/derived/na)
    float lo = 0, hi = 0;            // live/stepped: Mode sub-range in PLAIN units of the Mode's quantity
    Skew  skew;                      // track t<->Mode plain (lin/log/pow)
    bool  invert = false;            // track direction vs host param (remap: INPUT up = threshold down)
    float (*toHostPlain)(float modePlain) = nullptr;   // remap: INPUT dB -> threshold dB, dial 0..100 -> dB
    float (*fromHostPlain)(float hostPlain) = nullptr;
    std::span<const Detent> detents; // stepped (hard) or soft notches (live + soft=true)
    bool  soft = false;              // Glue-style magnetic notches on a continuous control
    float fixedPlain = 0;            // locked: the value shown; derived: filled per frame from DSP/UiFrame
    float defaultPlain;              // Mode default: the notch position and double-click target
    Formatter fmt;                   // plain -> {value, unit, sub}; Mode units (µS, dial numbers, "time per 10 dB")
};
SlotSpec presentation(const ModeSpec&, ParamId, const ParamSnapshot&);
}
```

Rules:
- **Hard detents** are hard: drag, wheel, keys, a11y and host automation (snapped by `effective()` on the audio thread, B §7.4.2) can only reach detents.
- **Soft notches** are an option for Modes that want marked "classic values" on a continuous control.
- **Locked and n/a never write the parameter.** The stored raw value survives, so switching back to another Mode restores the user's value. On hover the footer adds `STORED 3.0 MS (USED BY OTHER MODES)`. This refines B §7.4.2: snap only stepped params on a Mode switch, and never overwrite locked ones.
- **Sub-range:** the track spans the Mode's `[lo, hi]` at full resolution. A stored value outside it is shown clamped, and the footer says `CLAMPED FROM 5 µS`.
- **Direction invariant:** moving right always increases the quantity the slot's *label* names. A remap may therefore invert the host parameter (`invert = true`). The default notch and the bipolar fill follow the remapped direction.
- **Double-click resets to `defaultPlain`, the Mode's default,** not the host parameter default. The default notch is drawn there.

### 3.3 Slot anatomy per state

The HR geometry is kept (`buildLayout :178-240`, `drawControls :1502-1573`):
- Primary slot: 120 wide. Label at top+0, value at +22 (kValueP 24), sub line at +52 (kMicro), track at +72. Hit `{x−8, top−6, 136, 88}`.
- Secondary slot: 80 wide. Label +0, value +20 (kValueS 18), detent-label line at +38, track at +54. Hit `{x−6, top−6, 92, 70}`.

```
LIVE (continuous)                 STEPPED (hard detents, n=5)         LOCKED
THRESHOLD                         RATIO                               ATTACK               FIXED
-18.0 DB                          4:1                                 10 MS
DET -14.2                          4    8   12   20  ALL              T4 CELL, PROGRAM-DEP.
────────┃──┸─────╷────            ──┴────╂────┴────┴────┴──           · · · · · · ╻ · · · · · ·
        caret  notch  det-tick                                          (no caret)

DERIVED                           N/A                                 REMAPPED (primary)
RELEASE              = TIME       KNEE                                INPUT
2.0 S                             –                                   +24 DB
EFF 2.4 S                                                             THRESHOLD -28 DB
──────────▯────────               (no track)                          ─────────────┃──────
         hollow caret
```

| Element | live | stepped | locked | derived | n/a |
|---|---|---|---|---|---|
| Label (kLabel) | ink52 → ink70 on hover | same | **ink32**, hover ink52 | ink52 | **ink16**, hover ink32 |
| Tag (kMicro, right-aligned in the label row, `x+w`) | latch word if any | latch word | `FIXED` ink32 | `= RATIO`/`AUTO`/`= TIME` ink32 | – |
| Value (kValueP/S) | ink100, ink32 at default, accent under hand (HR `:1519-1521`) | same, prints the **detent label + unit** | **ink32** (never ink100 or accent) | **ink52**, live | `–` (U+2013) ink16 |
| Sub (primary) | live internals (§3.1) | **detent labels**, one per cell centre | reason (short) ink32 | live effective value ink32 | – |
| Track | 1 px hairline ink16, default notch 1×3 ink32, caret 2×(7+4h) | hairline plus **detent ticks** 1×5 ink32 at cell centres. Active tick ink100. Caret on the active detent (eased τ 60 ms). **Ghost caret** 1 px ink32 at the raw pointer position while dragging | **dotted**: 1×1 rrects every 4 px, ink16, plus a 1×5 ink32 notch at `fixedPlain` if it maps inside `[lo, hi]` | hairline ink16 plus a **hollow caret** (rrect 3×9, fill.a 0, border 1 ink52) at the derived position | none |
| Hover fill (accentDim) | yes | yes (to the active cell centre) | no | no | no |
| Cursor | LeftRightResize | LeftRightResize (PointingHand over a detent label) | Normal | Normal | Normal |
| Drag / wheel / keys / double-click | HR (`:1765-1823, 1840, 1916`) | index stepping (§3.4) | **refused**: no gesture, no write, footer shows the reason | refused | refused |
| Right-click | host menu (`:1716`) | host menu | host menu (harmless: the param exists) | host menu | host menu |

**Detent-label fit rule.** This is C's G6 text-fit lint, applied per Mode × dpi. Cell width `c = w/n`. Labels are drawn if `max textWidth(label, kMicro) + 4 ≤ c`, otherwise ticks only, and the readout and footer list the steps. At kMicro one character = 5.94 px (minus 1.4 on the last).

| Case | Slot | n | Cell | Widest label | Fits |
|---|---|---|---|---|---|
| 2-4-10 | primary 120 | 3 | 40 | "10" 10.5 | yes |
| 4-8-12-20-ALL | primary | 5 | 24 | "ALL" 16.4 | yes |
| SSL attack .1–30 | primary | 6 | 20 | ".1" 10.5 | yes |
| Glue attack .01–30 | primary | 7 | 17.1 | ".01" 16.4 | **no** (ticks only) |
| TIME 1–6 | primary | 6 | 20 | "1" 4.5 | yes |
| COMP/LIMIT | primary | 2 | 60 | "LIMIT" 28.3 | yes |
| VOICE A/E/LN | secondary 80 | 3 | 26.7 | "LN" 10.5 | yes |
| VOICE FLAT/HF… | secondary | 4 | 20 | "FLAT" 22.4 | **no** |

**Detents are laid out in index space, not value space,** so 0.1 ms and 30 ms both get a full cell. The track for a stepped slot therefore works as a segmented selector drawn as a rule. HR's Order cells are the same idea, drawn in the header (`:1113-1127`).

### 3.4 Interaction for stepped, remapped and locked slots

- **Drag (stepped).** Pixels per detent: `p = clamp(240/(n−1), 24, 64)` (n=3 → 64, n=5 → 60, n=7 → 40). Travel accumulates in index units. The value commits to the next detent when the travel from the current one reaches `0.5·p + 6 px` (6 px hysteresis), and re-anchors after each commit. Shift and Cmd (HR fine and ultra) are ignored. The pointer stays hidden (`enableUnboundedMouseMovement`, as HR). One gesture covers the whole drag.
- **Label click (stepped).** mouseDown on a label cell arms a jump. If the pointer moves more than 3 px before mouseUp, it becomes a relative drag from the current detent. Otherwise mouseUp commits `begin/set/end`, and nothing is written if the detent is unchanged (HR Order cell rule, `:1728-1741`).
- **Wheel (stepped).** One discrete notch = one detent. Smooth (trackpad) deltas accumulate until they equal one notch's delta; measure JUCE's per-notch `deltaY` on the target, because it depends on the platform. The gesture stays open with HR's 500 ms idle timer (`BgfxEditor.h:221`).
- **Keys.** See §8.
- **Soft notches (live + `soft`).** Within ±4 px of a notch, a drag sticks to it. Notch ticks are drawn 1×3 ink32.
- **Remap.** All input maps through `toHostPlain`. The display row shows the Mode quantity (`INPUT +24 DB`). The footer shows `INPUT (THRESHOLD)   RAISES LEVEL INTO A FIXED −X DBFS THRESHOLD   …`. The host lane prints the Mode text through B's Mode-aware `valueToText`.
- **Two universal params collapsed into one detent control** (Fairchild/MJUC TIMING). Attack is remapped to `TIME` with detents 1–6. Release is *derived*, tag `= TIME`, value = that detent's release, or `AUTO` with the effective time for the program-dependent 5–6. The DSP reads both from the one attack parameter.
- **Auto release: latch, not detent.** SSL and Glue put AUTO on the release knob. FCompressor keeps AUTO as the attached latch word in the **same place for every Mode**. When it is on, the Mode decides whether Release stays live as a scale (Pro-C semantics) or becomes derived. The alternative, a compound detent that writes two params in one gesture, is listed in §10.

### 3.5 Attached latch words

These are Pro-C's `AUTO` labels next to RELEASE and GAIN. Each is a kMicro word right-aligned in the slot's label row:
- Hit `{x+w−34, top−5, 38, 18}`. It is **carved out of the slot hit rect**: the hit test checks it first.
- Rest: ink32. On: ink100. Hover: ink70. Accent while pressed.
- Commits on mouseUp inside, and drag-off cancels. This is HR's Freeze latch behaviour.
- a11y role `toggleButton`.
- n/a (for example `LISTEN` in a Mode with no SC filter): the word is ink16 and refused. The footer gives the reason.

### 3.6 Mode selector and Mode browser

**Header latch** at x 624–920, y 14–68:
- `MODE` kCaption ink52, right-aligned at x 648, y 30. This is HR's `ORDER` caption idiom, `:1113`.
- `‹` chevron, hit `{656, 20, 24, 32}`. Name cell `{680, 20, 216, 32}`: kLatch 13 px, ink100, left at 688. `›` chevron, hit `{896, 20, 24, 32}`. Chevrons are 2 segments each (HR preset strip).
- Group caption under the name, kMicro ink32 at (688, 52): `FET · 3 OF 23`.
- Chevrons and the wheel over the name step through the global Mode order: groups in order, then introduction order within a group. The wheel needs a hit-test.
- Clicking the name opens the browser.
- It is not accented at rest. Accent job 1 only: accent while pressed.

**Browser overlay.** Rect `{40, 72, 880, 348}`, so y 72–420. It covers the display row and the band and leaves both control rows visible (the PresetPanel convention, `PresetPanel.h:14-18`):
- Opaque ground fill. Opens with HR's browser ease.
- **Columns = topology groups**, 6 columns of 146.7 px.
- Group heading at y 88, kCaption ink52, with the count: `FET  3`.
- Rows from y 108 on a 20 px pitch → 15 rows per column, down to y 408. **Capacity is 6×15 = 90 Modes** before a group needs a second column or the browser scrolls (wheel inside the browser).
- Row: name kLabel ink52. Hover ink100. Current Mode ink100 plus a 2×12 ink100 bar at x−8.
- The hovered row's one-line spec goes on the footer, for example `FET-76   FEEDBACK FET · PEAK · 20–800 µS · 4/8/12/20/ALL · INPUT DRIVES A FIXED THRESHOLD`.
- Commit on click (`mode` param `begin/set/end`, then B's snap of stepped params). Esc, clicking the latch again, or clicking outside cancels.
- Type-ahead: letters jump to the first Mode with that prefix, with a 1 s buffer.

**Landing feedback after a Mode switch.** It is truthful and has no overshoot:
- Carets ease to their snapped detents with τ 90 ms. This deliberately overrides HR's ">0.15 jump snaps" rule, because the move is the information.
- Slots whose `SlotSpec` changed show their label in ink100 for 0.6 s, then ease back.
- For 3 s the footer shows `FET-76 · 3 STEPPED · 1 FIXED · 2 DERIVED · 3 N/A`.
- The transfer curve eases between the old and new sampled curves over 160 ms, and the old curve stays as an ink16 ghost for the 0.9 s dwell.

Suggested groups, extensible (`ModeSpec::group`): **VCA, FET, OPTO, VARI-MU, DIODE, PWM/OTHER, MODERN** (lookahead/digital), **LIMIT**.

### 3.7 Meters (right column of the band)

- Bars `{833,164,10,240}` IN, `{867,164,10,240}` GR, `{901,164,10,240}` OUT. Centres 838, 872 and 906 on a 34 px pitch. Labels kMicro ink52 at y 416.
- **Same dB mapping as the band:** top = +6 dBFS, 5 px/dB at the 48 dB scale.
- IN and OUT:
  - Peak fill ink32, with RMS inside it as a 4 px-wide ink70 bar.
  - Peak hold is a 10×1 ink100 tick. It holds 1.5 s, then falls at 20 dB/s. Wall-clock seconds, HR convention.
  - The segment above 0 dBFS draws ink100. There is no red in the vocabulary.
- **GR hangs from the top** (0 dB GR = y 164), filled in `signal`. The max-GR hold tick is ink100.
- Readouts at y 148 (kMicro, centred on each bar): max peak and max GR since reset, clicking resets them (Pro-C). The widest readout, "-12.3", is 28.3 px, inside the 34 px pitch.
- a11y role `progressBar`, value "−4.2 dB".

### 3.8 Display row, latches, footer

- **Display row** at y 80–132, following HR `drawDisplay :1148-1200`:
  - Caption at (40, 80) kCaption ink52.
  - Value at (40, 96) kDisplay 44, with the unit on a shared baseline (kUnit 14).
  - **Default:** `GAIN REDUCTION −4.2 DB` in `signal` when live and non-zero, ink32 `0.0` when zero, and `–` when not live.
  - Touched, dragged or hovered slot: its label and value (HR precedence and 0.9 s dwell).
  - Hover on a band handle: that param.
  - Press-and-hold on the history: the values at the pointer's column, for example `AT −1.24 S` with `GR −3.8` large and `IN −8.1 · OUT −11.9` on the caption line.
- **DELTA** `{700, 92, 104, 40}` and **BYPASS** `{816, 92, 104, 40}` are HR latches, kLatch 13. Off: ink16 fill, ink52 text. On: ink70 fill, ground text. Armed: ink32. They commit on mouseUp.
- **Footer** at y 616, kLabel ink32. It holds the spec line: the first-run hint, the per-slot spec, reasons, remap aliases and the Mode-switch summary. A signature `GAIN COMPUTER / BALLISTICS / COLOUR / SDF` sits at the right, kCaption ink16 (HR idiom `:1603`).
  - First-run hint: `DRAG A VALUE OR THE CURVE. DOUBLE-CLICK TO RESET.`
  - Spec line for a stepped slot: `RATIO   STEPS 4 · 8 · 12 · 20 · ALL   DRAG / WHEEL / ARROWS STEP   CLICK A STEP   DBL-CLICK RESET`.

### 3.9 Archetype state matrix

This demonstrates every state. The names are neutral keys, as in B and C.

| Slot | `clean-vca` (MODERN) | `bus-g` (VCA) | `fet-76` (FET) | `opto-2a` (OPTO) | `vari-mu` (VARI-MU, 670-style) |
|---|---|---|---|---|---|
| THRESHOLD | live | live | **remap INPUT**, 0..+48 dB, invert, sub `THRESHOLD −28 DB` | **remap PEAK RED.**, dial 0..100, invert | live |
| RATIO | live 1..∞ | **stepped 2/4/10** | **stepped 4/8/12/20/ALL** | **stepped COMP/LIMIT** | **n/a**: ratio rises with level (remote-cutoff tube), see curve |
| KNEE | live | **derived `= RATIO`** (soft/medium/hard) | **locked** (circuit knee) | **n/a** (curve is the cell's) | **n/a** |
| ATTACK | live | **stepped .1/.3/1/3/10/30 ms** | live, sub-range 0.02–0.8 ms, sub `DIAL 4.2` | **locked ≈10 ms** | **remap TIME, stepped 1–6** |
| RELEASE | live, AUTO live | **stepped .1/.3/.6/1.2 s**, AUTO live (derived when on) | live 50–1100 ms, sub `DIAL` | **derived** (two-stage, `EFF` live), AUTO n/a | **derived `= TIME`** (5–6 `AUTO`) |
| MAKEUP | live, AUTO live | live | **remap OUTPUT** | **remap GAIN** | **remap OUTPUT** |
| MIX | live | live | live | live | live |
| RANGE | live | live | n/a | n/a | n/a |
| HOLD | live | n/a | n/a | n/a | n/a |
| LOOKAHEAD | live | n/a | n/a | n/a | n/a |
| DETECT | live, EXT live | locked `RMS`, EXT live | locked `PEAK · FEEDBACK` | locked `CELL` | locked `RMS` |
| SC HPF | live, LISTEN | live | live (modern add-on) | live | live |
| LINK | live | live | live | live | live |
| DRIVE | n/a (no colour stage) | live | live | live | **remap INPUT** (tube drive) |
| VOICE | n/a | n/a | **stepped A/E/LN** | **stepped FLAT/HF** (ticks only, §3.3) | **stepped L/R, M/S** |

---

## 4. The characteristics band

### 4.1 Why a band and not a separate screen

- HR's rule is that everything is visible with no disclosure (`BgfxEditor.h:17-18`). A's §6.6 already maps "characteristics screen" to a band with views.
- The band is where a compressor's truth lives. Hiding it behind a page would hide GR while the user turns Attack.
- The HR mechanism (view follows the touched control) is the right interaction: turn Attack and see the step response; release and see the history again.
- **Optional P2, not v1:** an `EXPAND` word at the right end of the tab row could grow the band over the secondary control row (y 164–604, 440 tall) with INTERNALS always on. It is disclosure, so it is flagged in §10.

### 4.2 Geometry and shared scale

- Regions:
  - Region H (history family): `{40, 164, 500, 240}`
  - Region T (transfer family): `{560, 164, 240, 240}`
  - Meters: `{824, 164, 96, 240}`
- Tab row at y 148, kCaption:
  - Region H tabs: `HISTORY TIME SIDECHAIN INTERNALS` at x 40/98/136/207. Span `2.5 5 10 20 S` kMicro, right-aligned to 540.
  - Region T tabs: `CURVE COLOUR` at x 560/604. Scale `12 24 48 72 DB`, right-aligned to 800.
  - Active tab ink70, hover ink100, rest ink32 (the theme-cell idiom, not accent).
- Axis labels at y 416, kMicro ink32. State lane at y 407–410.
- dB mapping for all three regions: `y(db) = 164 + (6 − db)·(240/S)`, where S ∈ {12, 24, 48, 72} dB → 20 / 10 / **5** / 3.33 px/dB. The top is +6 dBFS.
  - At S = 48: 0 dBFS → y 194, −12 → 254, −24 → 314, −36 → 374, floor −42 → 404.
  - Transfer x uses the same mapping, so the plot is square and unity is exactly 45°: `x(db) = 560 + (db + 42)·5`, giving −36 → 590, −24 → 650, −12 → 710, 0 → 770, +6 → 800.
- Grid: ink16 hairlines at −12-dB steps (6 dB at S ≤ 24). Horizontal lines run across H and T. Vertical lines only in T. A 0 dBFS hairline is always drawn.
- **Persistence:** scale and span are machine-wide `UiPreferences` like the theme ("new instances open with the chosen scale", Pro-C). Pinned tabs are per-instance UI state in the plugin's state tree, not parameters.

### 4.3 Region H views

**HISTORY** (default pinned). Time runs right to left and "now" is x 540.
- **250 columns of 2 px.** Column duration = span/250: 10 / 20 / 40 / 80 ms. Scrolling is smooth: the strip is shifted by the fractional column phase each frame.
- **IN** = input peak per column: a `KIND_AREA` from the floor up to the level, fill ink16, with a top stroke of 1 px ink32.
- **OUT** = output peak (post-makeup, post-mix): a stroke-only `KIND_AREA` (fill α 0, top stroke 1 px ink70). This gives a seam-free line.
- **GR** hangs from y 164: an area with fill = `mix(ground, signal, 0.18)` pre-mixed and **a bottom-edge stroke** 1.5 px in `signal`.
- **DET** (detector level, ink52 1 px, stroke-only area) is drawn **only when it can differ from IN**: SC HPF ≠ OFF, DETECT > 0, EXT on, LINK < 100 %, or a feedback Mode. This keeps HISTORY truthful when the threshold line is compared against a filtered detector.
- **Threshold line:** one `hairlineH` at `snapY(y(T_eff))` from x 40 to 800. It crosses the 20 px gap and ends on the transfer handle. ink32 at rest, accent while THRESHOLD is under the hand or the line is dragged. With a remap, `T_eff` is the effective threshold at the plugin input, for example `T_fixed − inputGain`.
- **State lane** under the axis at y 407–410:
  - The per-column ballistic phase: ATTACK ink70, HOLD ink100, RELEASE ink32, idle nothing.
  - Runs of equal state merge into one rrect, typically ≤40 primitives.
  - This is Compassion's green/red attack/release bands, expressed in ink.
- Time labels at y 416: `0` at 540, then `−1`…`−5 S` every 100 px at 5 s.
- **Audio time, not wall-clock time.** The history advances only as ring points arrive. When the stream goes stale (0.5 s, HR `isLive`), it holds and dims to 50 % over 0.4 s. It does not scroll silence it never received.

**TIME** (auto-shown while ATTACK, RELEASE, HOLD, AUTO, DETECT or LOOKAHEAD is touched): the Compassion Response graph, made exact.
- Two panes:
  - **ATTACK** `{40,164,230,240}`: log time 0.01 ms–1 s, 5 decades, 46 px/decade, labels `.01 .1 1 10 100 MS 1 S`.
  - **RELEASE** `{310,164,230,240}`: log time 1 ms–10 s, 4 decades, 57.5 px/decade.
- **y = fraction of the target GR**, hanging downwards like GR everywhere else: 0 % at y 184, 100 % at y 384. The absolute target is printed in the pane caption: `TARGET −7.5 DB AT +10 DB OVER`.
- Stimulus: the level steps to +X dB over threshold at t=0 (attack pane) and back down at t_off (release pane).
  - Attack pane: **three curves for X = +6, +12 and +24 dB** (ink32, ink52, ink70; accent for +12 while a time control is under the hand). This makes level-dependent attack visible.
  - Release pane for Modes with auto or program-dependent release: **after a 50 ms burst vs after a 2 s burst**. This shows Glue/SSL two-stage and opto memory.
- Markers:
  - **Declared spec:** an ink32 hairlineV at `attackSpec().seconds` and at the release equivalent, using the Mode's `TimeLaw` (C §5.1: 63 % for `expDb`, 10–90 for `t10_90`), labelled `0.30 MS`.
  - **Measured crossing** of the nominal curve with that law's level: a 5 px ink100 ring.
  - If the two do not coincide, the display is showing a real discrepancy. That matches C's D2 probe.
- In stepped Modes, the other detents' nominal curves are drawn as ink16 ghosts.
- Hold shows as a flat segment at the start of the release pane.
- Source: `ModeSpec::renderStepResponse` (§6.3), run on the message thread only while TIME is visible. It is throttled to 20 Hz during drags and cached by snapshot hash.

**SIDECHAIN** (auto while SC HPF, LISTEN or EXT is touched). This is HR's filter view (`drawFilterView :1232-1320`), reused:
- log x 20 Hz–20 kHz over 500 px (3 decades, 166.7 px/decade). y 0 to −24 dB, y 184–384, 8.33 px/dB.
- The Mode's detector-filter magnitude curve: 1.5 px accent while touched, ink70 otherwise. A corner-frequency tick with a label.
- Caption: `SIDECHAIN – INTERNAL`, or `– EXTERNAL · −14 DB PK` when EXT is on, plus `LISTENING` in ink100 when LISTEN is on.
- A spectrum is not in v1.

**INTERNALS** (pinned by tab, and auto while LINK is touched): Compassion/DC8C depth.
- Traces `{40,164,380,240}` on the same time axis as HISTORY (190 columns × 2 px), with the same px/dB:
  - DET (ink52)
  - **TARGET GR** (static computer output, ink32)
  - **APPLIED GR** (post-ballistics, `signal`)

  The gap between target and applied *is* the ballistics.
- Mode internal channels flagged `history` get normalised 40 px lanes at the bottom, for example `OPTO MEMORY 0–100 %`, `SLOW STAGE GR`, `TUBE BIAS`.
- **Legend** `{432,164,108,240}`, kMicro rows on a 14 px pitch, name ink52 left, value ink100 tabular right:
  - `DET −14.2`, `TARGET −5.1`, `APPLIED −3.8`, `PHASE REL`, `REL EFF 420`, `EFF RATIO 3.2`, `CREST 11.4`
  - `DET L/R −14.2/−15.0` when LINK < 100 %
  - then up to 8 Mode internals with their names, units and formats from `ModeSpec::internals[]`.

### 4.4 Region T views

**CURVE** (default pinned):
- **Unity diagonal:** 1 segment, ink16, (560,404)→(800,164).
- **Static curve:** the Mode's `staticGainDb` (gain computer only, pre-makeup and pre-mix).
  - Sampled **non-uniformly**: 64 points uniform over the axis, plus 32 inside `[T−W/2, T+W/2]`, plus 16 around the range break-point. That is ≈112 segments, which meets C's G2 chord error ≤0.25 px.
  - Custom curve families (vari-mu, opto): 120 uniform points.
  - ink70 1.0 px at rest. **Accent 1.5 px** while THRESHOLD, RATIO, KNEE or RANGE is under the hand (HR filter-view convention).
  - Drawn in **pre-mixed opaque colours** to avoid bead artefacts in crossfades (A §2.6).
- **GR wedge:** the area between unity (top) and curve (bottom) above threshold. 60 `KIND_AREA` columns of 4 px, fill ink16.
- **Knee region:**
  - Two ink16 `hairlineV`s at x(T±W/2) from the curve down to the floor.
  - A bracket on the floor: `hairlineH` at y 400 from x(T−W/2) to x(T+W/2), with 1×4 end ticks, ink32.
  - Label `KNEE 6 DB` in kMicro above it. It reads `FIXED` or `= RATIO` when locked or derived, and there is **no bracket when knee is n/a**: absent, not faked.
- **Net curve** (ink32, 1 px): `20·log10(mix·10^((g+mk)/20) + (1−mix))`, where g = static gain and mk = makeup. It is drawn only if makeup ≠ 0 or mix < 100 %. This is exact at steady state because the dry path is latency-aligned (B §7.4.5). It is highlighted in accent while MAKEUP or MIX is under the hand.
- **Ghost curves:** for stepped RATIO (or KNEE), the other detents in ink16. Hovering a detent label previews that curve in ink32.
- **Operating point** (live only):
  - x = `curveXDb`, the level on the curve's input axis. The Mode supplies it: the detector level for feed-forward, the detector-law level of the *input* for feedback, so the static curve stays valid.
  - y = `curveXDb + grAppliedDb`.
  - Disc r 3.5, ink100.
  - **GR needle:** a 2 px `signal` rrect from unity y(curveXDb) down to the dot. It is the same length as the GR meter bar and the history's GR depth.
  - **Trail:** the last 32 points at 10 ms spacing, from the UI history store. A polyline 1 px from ink70 fading to ink16 in pre-mixed colours. It shows the attack and release loop above and below the static curve.
- **Handles**, drawn when the pointer is over region T or during a drag (hover amount 90/160 ms), and always when the always-chrome fallback is on (`:1507`):
  - **Threshold:** a 7×7 ring (ink70, border 1, filled with ground) at (x(T), snapY(y(T))) on unity. Hit 16×16. LeftRightResize. Drag moves along x.
  - **Knee:** two 5×5 rings at x(T±W/2) on the curve. Dragging either one changes W symmetrically.
  - **Ratio:** a 5×5 ring on the curve at x = min(T+12, +3) dBFS. UpDownResize. Vertical drag. In stepped Modes it snaps to detent curves with 6 px hysteresis, measured at the handle's x.
  - **Range:** a 5×5 ring at the break point where the curve returns to slope 1, if it is inside the plot.
  - **Locked or derived param:** the handle is drawn as a 1 px ink32 cross, not a ring, and is not draggable (Normal cursor). **N/A:** no handle.
- Caption above T: `CURVE – GAIN COMPUTER`. On a Mode switch the old curve stays as an ink16 ghost for 0.9 s (§3.6).

**COLOUR** (auto while DRIVE or VOICE is touched):
- Linear axes −1..+1 on both x and y. Identity ink16.
- The Mode's colour-stage waveshaper at the current DRIVE and VOICE: 128 segments, accent while touched.
- **Live input-peak markers:** ink52 hairlineV at ±`colourInPeak`, showing where on the curve the signal is working.
- Text, kMicro:
  - Top left, ink52: `TUBE · DRIVE 40 %`.
  - Bottom, ink32: `H2 −41 · H3 −28 DB AT −6 DBFS`, computed by a 64-point DFT of one cycle of `shape(sin)`.
  - For stateful stages (hysteresis, bias drift) the caption reads `STATIC APPROXIMATION`.
- Mode with no colour stage: `NO COLOUR STAGE IN CLEAN-VCA` centred in ink32, and nothing else.

### 4.5 Switching views

- **Two `DwellSelector`s**, one for H and one for T. This is A §6.5 "DwellSelector/BandView", lifted from `:996-1031`:
  - `target = dragged ? viewFor(dragged) : (touchSource within 0.9 s ? viewFor(source) : pinned)`
  - `amt` eases with τ 0.18 s and snaps within 1e−3.
  - The outgoing view draws at `1−amt`.
- **Hover never switches views.** A drag holds the view. Wheel, key or a11y writes hold it for the 0.9 s dwell.
- Clicking a tab **pins** the view (and shows it immediately). The first-run pinned views are HISTORY and CURVE.

| Touched | Region H | Region T |
|---|---|---|
| THRESHOLD, RATIO, KNEE, RANGE (slot or handle) | HISTORY (threshold line accent) | CURVE (curve accent) |
| ATTACK, RELEASE, HOLD, AUTO(rel), DETECT, LOOKAHEAD | TIME | CURVE |
| SC HPF, LISTEN, EXT | SIDECHAIN | CURVE |
| MAKEUP, AUTO(gain), MIX | HISTORY | CURVE (net curve accent) |
| LINK | INTERNALS | CURVE |
| DRIVE, VOICE | pinned | COLOUR |
| MODE switch | pinned | CURVE (with the previous-curve ghost) |

- **HISTORY ingestion never stops** while another view is shown. The ring is drained every frame, so returning to HISTORY shows continuous data.

### 4.6 Pointer interaction in the band

| Where | Gesture | Effect | Discipline |
|---|---|---|---|
| T handles | drag | writes the param. Mapping is **absolute** (the handle follows the pointer); Shift = relative at 1/5 gain, re-anchored on modifier change (HR `:1765-1790`) | begin on down, end on up and in the dtor (HR `:163-175`) |
| T handles | double-click | reset to the Mode default | begin/set/end |
| T handles | right-click | host param menu (HR `:1716`) | – |
| History threshold line (±4 px) | vertical drag | THRESHOLD (via the remap inverse) | as above, UpDownResize |
| History elsewhere | press and hold | **freeze** the history. An ink52 vertical cursor line follows the pointer and the display row shows that column's values. Release resumes | no param write |
| Band | wheel | only over a handle: steps that param (HR wheel rules) | 500 ms idle gesture |
| Tabs, span, scale words | click | pin view or set preference | – |

---

## 5. Wireframes (logical px, 960×640)

### 5.1 Main panel

```
x→ 0   40                  228                               600 624     688                       920 960
y  +----------------------------------------------------------------------------------------------------+
14 |   F COMPRESSOR         ‹ ANALOGUE BUS 02            ☆ ›        MODE ‹ FET-76                    ›   |  header 0-64
46 |   FET · FEEDBACK · PEAK   BUS · DRUMS                               FET · 3 OF 23   GRAPHITE PAPER   |  preset strip 228-600 x 14-58
80 |   GAIN REDUCTION                                                    +----------+ +----------+       |
96 |   -4.2 DB  (kDisplay 44, signal)                                    |  DELTA   | |  BYPASS  |       |  latches y 92-132
   |                                                                     +----------+ +----------+       |
148|   HISTORY TIME SIDECHAIN INTERNALS   2.5 5 10 20 S   CURVE COLOUR  12 24 48 72 DB  -3.1 -6.2 -1.0    |  tab row
164|   +--------------------------------------------------+  +------------------------+ ▐▌   ▐▌   ▐▌       |
   |   | GR ▔▔▔▔\__/▔▔▔▔▔▔\____/▔▔▔▔▔▔ (signal, hangs)    |  |                     ·╱ | ▐▌   ▐▌   ▐▌       |
194|   |0 ─────────────────────────────────────────────── |  |- - - - - - - - - - ╱- | ▐▌        ▐▌       |  0 dBFS
   |   |   ░▒▓▒░▓▓▓▒░▓▓▓▓▒░▒▓▓░  IN area / OUT line       |  |               ,──●'    | ▐▌        ▐▌       |  op dot + needle
284|   |━━━━━━━━━━━━━━━━━━━ THRESHOLD ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━◇  ▒▒ wedge | ▐▌        ▐▌       |  line → handle
   |   |                                                  |  |           ╱            |                   |
374|   |-36                                               |  |         ╱   ⌐KNEE 6¬   |                   |
404|   +--------------------------------------------------+  +------------------------+                   |
408|   ▬▬▬ ▬ ▬▬▬▬ ▬  (state lane: ATTACK/HOLD/RELEASE)                                                    |
416|   -5 S      -4        -3        -2        -1       0    -36   -24   -12    0      IN   GR   OUT       |
440|   THRESHOLD           RATIO               ATTACK              RELEASE     AUTO    MAKEUP      AUTO   MIX|
462|   -18.0 DB            4:1                 250 µS              120 MS              +4.0 DB            100 %|
492|   DET -14.2            4   8  12  20 ALL  DIAL 4.2            EFF 340 MS          AUTO +4.1          DRY -INF|
512|   ──────┸──┃──        ─┴───╂───┴───┴───┴─  ──────┃─────        ──┃───────          ─────┃──           ──────┃|
540|   KNEE     RANGE    HOLD     LOOKAHEAD DETECT EXT SC HPF LISTEN LINK   DRIVE    VOICE                    |
560|   6 DB     40 DB    –        –         PEAK     OFF            100 %   0 %      E                        |
578|   FIXED                                                                         A  E  LN                 |
594|   · · · ·  ──┃───           ─────     ─┃───  ─────           ────┃ ─┃────  ─┴─╂─┴─                     |
616|   RATIO   STEPS 4 · 8 · 12 · 20 · ALL   DRAG / WHEEL / ARROWS STEP   CLICK A STEP        GAIN COMPUTER / … / SDF|
640+----------------------------------------------------------------------------------------------------+
```

(Character columns are approximate. The coordinates below are exact.)

| Element | Rect / position (x, y, w, h) |
|---|---|
| Wordmark | "F" (40,24) kWordmark ink100; "COMPRESSOR" at 40+w("F")+9 ≈ 55, ink52 (ends ≈137) |
| Mode caption | (40,46) kCaption ink32. The Mode's `topologyLine` |
| Preset strip | {228,14,372,44} (HR's 204–572 moved 24 right; PresetPanel geometry must be parameterised, A §1.2) |
| Mode latch | caption right-aligned at 648,30; ‹ {656,20,24,32}; name {680,20,216,32}; › {896,20,24,32}; group (688,52) kMicro |
| Theme cells | {790,52,72,16} GRAPHITE, {872,52,48,16} PAPER |
| Display | caption (40,80); value (40,96) kDisplay; unit on shared baseline |
| DELTA / BYPASS | {700,92,104,40} / {816,92,104,40} |
| Tab row | y 148. H tabs x 40/98/136/207; span words right-aligned to 540; T tabs 560/604; scale words right-aligned to 800; meter readouts centred 838/872/906 |
| Region H | {40,164,500,240}; state lane {40,407,500,3}; time labels y 416 |
| Region T | {560,164,240,240}; dB labels y 416 at x 590/650/710/770 |
| Meters | bars {833,164,10,240} {867,164,10,240} {901,164,10,240}; labels y 416 |
| Primary slots | x = 40 + 152·i (40,192,344,496,648,800), w 120, top 440: label 440, value 462, sub 492, track 512; hit {x−8,434,136,88} (16 px dead gutter) |
| Secondary slots | x = 40 + 100·i (40…840), w 80, top 540: label 540, value 560, detent labels 578, track 594; hit {x−6,534,92,70} (8 px gutter) |
| Attached words | {x+w−34, top−5, 38, 18}, carved out of the slot hit |
| Footer | spec (40,616) kLabel ink32; signature right-aligned 920,618 kCaption ink16 |
| Overlays | Mode browser and preset browser {40,72,880,348} |

### 5.2 Characteristics band, CURVE + HISTORY (S = 48 dB)

```
        x 40                                                   540  560                                800
y 148   HISTORY  TIME  SIDECHAIN  INTERNALS        2.5 5 10 20 S     CURVE  COLOUR          12 24 48 72 DB
  164   ┌──────────────────────────────────────────────────────┐    ┌──────────────────────────────────────┐ +6
        │▒▒▒▒▒▒▒╲___╱▒▒▒▒▒▒▒▒▒▒▒╲______╱▒▒▒▒  GR (signal, 5px/dB)│    │                                  ·╱  │
  194   │0 ···································· 0 dBFS ··········│    │·································╱····│ 0
        │                     .-.     ._.                        │    │              ○ratio      ___---'     │
        │   ___.---.__.-----'   '---'   '--.___  OUT (ink70)     │    │                   ___---'  ║ needle  │
  254   │-12 ·······▓▓▓▓▓·····▓▓▓▓▓▓·····▓▓▓▓▓▓··· IN (ink16 area)│    │·············__●'···········║·········│ -12
        │     ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓          │    │       ▒▒▒▒_-'  trail                 │
  284   ├━━━━━━━━━━━━━━━━━━━━━━ threshold T −18 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━◇ (680,284)             │
        │     ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓          │    │      ○╱│knee handles at T±W/2        │
  314   │-24 ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│    │·····╱··│··········│····················│ -24
        │                                                        │    │   ╱    │          │                    │
  374   │-36                                                     │    │·╱······│··········│····················│ -36
  400   │                                                        │    │╱       └─KNEE 6 DB┘                  │
  404   └──────────────────────────────────────────────────────┘    └──────────────────────────────────────┘ -42
  407   ▬▬▬▬ ▬ ▬▬▬▬▬▬ ▬▬ ▬  state lane (ATTACK ink70 / HOLD ink100 / RELEASE ink32)
  416   -5 S        -4        -3        -2        -1          0      -36        -24        -12         0 DB
```

### 5.3 TIME view (region H), step response

```
        x 40                        270   310                              540
y 148   HISTORY [TIME] SIDECHAIN INTERNALS
  164   ATTACK  TARGET −7.5 DB AT +10 OVER      RELEASE  AFTER 50 MS / AFTER 2 S
  184   0 %  ───╮                                100 %  ╲─╮___                       ← fraction of target GR, hangs down
        │       ╲╲╲  +24 +12 +6                           ╲   ╲__  (after 50 ms)
  310   │ 63 %·······╲╲·╲·········  ○ measured          ···╲·····╲__···· 63 % ········
        │          │  ╲╲ ╲  ghosts (other detents ink16)     ╲       ╲___ (after 2 s)
  384   100 % ─────│───╲╲─╲───────                     0 %  ─╲──────────╲______
        .01  .1  1 │ 10  100 MS 1 S                     1   10  100 MS  1   10 S
                   └ declared spec 0.30 MS (hairlineV ink32)
```

### 5.4 Mode browser overlay

```
y 72  +----------------------------------------------------------------------------------------------+
  88  | VCA  2           FET  3           OPTO  2          VARI-MU  2       DIODE  1        MODERN  4  |
 108  | CLEAN VCA        FET-76           OPTO-2A          VARI-MU          DIODE-609       CLEAN      |
 128  |▌BUS-G            FET-76 REV E     OPTO-3A          MU-TWIN                          TRANSIENT  |
 148  |                  FET-ALL                                                            MASTER     |
      |                                                                                    LOOKAHEAD  |
 408  |                                    (15 rows × 6 columns = 90 before it scrolls)                 |
 420  +----------------------------------------------------------------------------------------------+
 440   (control rows stay visible: switching shows their constraints landing)
 616   footer: hovered Mode's spec line
```

### 5.5 Slot states at pixel scale (primary, 120×88)

```
top+0   THRESHOLD ............................   label kLabel 11px (ink per state)      tag right-aligned (kMicro)
top+22  -18.0 DB                                  value kValueP 24px + unit kLabel
top+52  DET -14.2 | detent labels | reason        kMicro 10px
top+72  ────────────┃───┸──────                   track 1px; caret 2×7..11; notch 1×3; detent tick 1×5
top+76              ╵ detector tick 1×4 ink52 (THRESHOLD only)
```

---

## 6. Drawing primitives: inventory and gaps

### 6.1 Per element

| Element | Primitive(s) | Count / frame | Available today? |
|---|---|---|---|
| All text | `text` (SDF) | ≈230 static (panel) + ≈40 live readouts | yes. **Needs µ (U+00B5)**. Optional − (U+2212) and … (U+2026). All 4 steps in A §4 (subset regen, FontProbe re-bless) |
| Slot tracks, notches, carets, detent ticks, dotted locked tracks | `rrect`, `hairlineH/V` | ≈15×6 + ticks ≈30 + dots (≤30 per locked slot) ≈ 150–250 | yes (dots = 1×1 rrects) |
| Hollow caret (derived), handle rings | `rrect` fill.a 0 + borderW (HR ring trick, A §2.1) | ≤10 | yes |
| Latches, meter bars, peak ticks, GR needle | `rrect` | ≈20 | yes |
| Grids, axes, threshold line, knee lines, 0 dBFS | `hairlineH/V` | ≈25 | yes |
| History IN area with top stroke | **`KIND_AREA`** column | 250 | **no** (A §2.6 proposal) |
| History OUT line, DET line | `KIND_AREA` stroke-only (fill α 0). Seam-free, fades without beads | 250 (+250 DET, conditional) | **no** |
| History GR hanging area with **bottom-edge** stroke | `KIND_AREA` + **stroke-edge flag** | 250 | **no**. A's spec strokes the top edge only; add a flag bit |
| GR wedge in CURVE | `KIND_AREA` 4 px columns | 60 | **no** |
| INTERNALS traces and lanes | `KIND_AREA` stroke-only | 190 per trace × 3–5 | **no** |
| Static curve, net curve, ghosts, trail, step responses, SC curve, colour curve | `segment` polyline, pre-mixed opaque colours | 112 + 112 + ≤2×112 + 32; TIME ≈6×120; SC 160; COLOUR 128 | yes (capsules). Add a `polyline()` wrapper |
| Operating dot | `rrect` r = w/2 | 1 | yes. Add a `disc()` wrapper |
| State lane | `rrect` runs | ≤40 | yes |
| Chevrons | 2 × `segment` | 4 | yes (HR preset strip) |
| Overlays (browsers) | opaque `rrect` + text | ≈120 when open | yes |

### 6.2 Missing from SdfCanvas today, and what A recommends

1. **`KIND_AREA = 3`**. A §2.6 gives the shader branch, payload and FrameRender mirror. Branch chain `<0.5 / <1.5 / <2.5 / else`.
   - **Addition from this design:** use `d2.w` as a flags word. Bit 0 = live (A's proposal). **Bit 1 = stroke the bottom edge** instead of the top, needed for GR hanging from the top.
   - For symmetry, bit 2 = stroke both edges (the band outline for the "removed" region, if it is ever wanted).
   - FrameRender must decode the flags as integer bits of a float (`int(d2.w + 0.5)`).
2. **Live flag + `SdfCanvas::setLive(bool)`**, and **semantic tags plus `axis` lines** in the dump (C §3.3 #4). Most of the band is live, so HR's y-band exclusion heuristic (`FrameRender.cpp:122-127`) cannot work here.

   Tags this design emits:

   | Tag | Primitives |
   |---|---|
   | `TRANSFER_CURVE` | static curve |
   | `NET_CURVE` | net curve (makeup/mix) |
   | `THRESHOLD_MARK` | threshold handle and line |
   | `KNEE_MARK` | knee lines, bracket, handles |
   | `OP_DOT` | operating point |
   | `GR_NEEDLE` | GR needle on the curve |
   | `GR_METER` | GR bar and hold tick |
   | `HIST_IN`, `HIST_OUT`, `HIST_GR`, `HIST_DET` | history traces |
   | `STEP_RESP` | TIME curves |
   | `SC_CURVE` | sidechain curve |
   | `COLOUR_CURVE` | colour transfer |
   | `DETENT_TICK` | slot detent ticks |
   | `SLOT_CARET` | slot carets |

   These feed C's G2 (curve vs analytic) and G3 (dot and meters tell the truth).
3. **CPU wrappers (no shader change):**
   - `polyline(xs, ys, n, w, col)`
   - `disc(cx, cy, r, fill, ringW, ring)`
   - `areaStrip(xs, yTop, yBot|null, n, yBase, fill, strokeW, stroke, flags)`
   - `dotted(x, y, w, step, col)`
4. **Transient budget.** Typical frame ≈1.4K primitives (≈300 static + ≈790 history + ≈300 curve + ≈15 meters). Worst ≈1.9K (DET on, ghosts, INTERNALS). At 384 B per primitive that is 0.54–0.73 MB/frame, so only 8 editors fit in HR's 6 MiB before frames drop silently (`SdfCanvas.cpp:322-329`).
   - **Set `init.limits.maxTransientVbSize = 32<<20`** (A §2.6 #6 (i)), and make overflow visible.
   - Later: indexed quads (268 B per primitive).
5. **No clip stack.** All plot geometry is clamped on the CPU to its region (`jlimit`, as HR `:1317-1318`). The curve, trail and step responses must clamp to their rects. The history's fractional scroll offset must clip the oldest column at x 40 by trimming its x0.
6. **Canvas as a member,** not constructed per frame (A §2.1). It avoids reallocating ≈120 KB of vertices every frame.
7. **Not needed:** gradients, arcs, textures. A history texture (A §2.6 #7) is not worth it at 250 columns.

---

## 7. Data the UI needs from the DSP, and update rates

### 7.1 `UiFrame` (seqlock, latest wins, published per block while `uiAttached_`, HR pattern `FDNReverb.cpp:1190-1272`)

```cpp
struct UiFrame {                  // 38 x 4-byte words = 152 B; static_assert(sizeof % 4 == 0) as HR
    uint32_t publishCount;
    int32_t  modeIndex;           // Mode the DSP is actually running (lags the param during a switch crossfade)
    uint32_t stateBits;           // b0-1 phase {0 idle,1 attack,2 hold,3 release}; b2 auto-release slow stage active;
                                  // b3 at range limit; b4 external SC present; b5 delta; b6 bypass; b7 mode crossfade
    float sampleRate;
    // effective values after Mode snap/remap/lock and smoothing (what the audio is really using)
    float thrEffDb, ratioEff, kneeEffDb, rangeDb, attackEffSec, releaseEffSec, holdSec, makeupEffDb, mix, driveEff;
    // instantaneous state
    float curveXDb;               // level on the static curve's input axis (FF: detector; FB: input in detector law)
    float detDb[2];               // per-channel detector level before link
    float grTargetDb, grAppliedDb;// static computer output at curveXDb vs post-ballistics gain (<= 0 for downward)
    float grBlockMinDb;           // most reduction inside this block
    float inPeak[2], inRms[2], outPeak[2], outRms[2];   // linear; instant attack, 40 ms release (HR law)
    float scPeak;                 // external sidechain peak (linear) or 0
    float colourInPeak;           // level into the colour stage (linear)
    float internals[8];           // Mode-defined; ModeSpec::internals[i] = {name, unit, lo, hi, fmt, history?}
};
```

Why effective values are included even though the UI can read the params:
- Auto makeup, program-dependent attack/release, derived knee and auto-release state exist only in the DSP.
- Smoothing means the audio lags the parameter.

When the stream is not live (0.5 s stale, HR `isLive`), the slots use the parameter-derived model (`presentation()` + `fixedPlain`). Derived "EFF" readouts are *absent*, not guessed.

### 7.2 `HistoryRing` (SPSC, audio→UI; B §3, A §2.6)

```cpp
struct HistoryPoint {             // 32 B, one per 1 ms of AUDIO time (fractional phase accumulator: exact at 44.1k)
    float inPk, outPk;            // linear max |x| over the ms (both channels)
    float detDb;                  // max detector (curve-axis) level over the ms
    float grTargetDb, grAppliedDb;// min (most reduction) over the ms
    float int0, int1;             // the Mode's two history-flagged internals (last value)
    uint32_t stateBits;           // phase at the sample of max applied GR; b2 slow stage; b3 range
};
// capacity 4096 points (4.1 s, 128 KiB); atomic<uint64_t> writeCount (release); reader keeps lastRead.
// Overrun (writeCount - lastRead > capacity): the UI inserts a gap marker and resyncs. The audio thread writes only while uiAttached_.
```

**UI-side store:** 5 ms resolution × 4096 entries = 20.5 s, enough for the 20 s span. Each entry is max/min over 5 points. Columns are rebuilt every frame from the store: 2/4/8/16 entries per column at 2.5/5/10/20 s. The trail reads the last 32 entries at 10 ms spacing.

### 7.3 `ModeSpec` UI hooks (additions to C §5.1)

| Hook | Signature (proposal) | Used by | Cost |
|---|---|---|---|
| `presentation` | `SlotSpec presentation(ParamId, const ParamSnapshot&)` | slots, a11y, host `valueToText`, probes | trivial, per frame per slot (cached by snapshot hash) |
| `staticGainDb` | exists in C §5.1 | CURVE, net curve, ghosts, `EFF` ratio (numeric derivative) | ≈112 evaluations per param change |
| `renderStepResponse` | `void (const ParamSnapshot&, double fs, const StepStimulus&, float* grDbOut, int n)`. Runs the Mode's **own** detector + gain computer + ballistics on a level-domain stimulus (for example a Nyquist-rate ±A square injected after the SC filter, so peak = RMS = A). **A first-class API, not C's test-only `FCMP_TEST_TAP`**, because the shipping UI calls it | TIME view | attack pane 1.1 s + release pane 2×(burst + 10 s) at fs, ≈1.3M samples at 48 kHz for all curves. **Run it on a background thread**, or decimate the release pane (see note) |
| `scResponse` | `float scMagnitudeDb(const ParamSnapshot&, double fs, float hz)` | SIDECHAIN | 161 points per change |
| `colourTransfer` | `float colour(float x, const ParamSnapshot&)` + `bool colourIsStatic` + `bool hasColour` | COLOUR | 128 points plus a 64-point DFT |
| `internals[]` | `{name, unit, lo, hi, fmt, bool history}` ×≤8 | INTERNALS legend and lanes, a11y | – |
| `group`, `topologyLine`, `specLine` | strings | header caption, browser, footer | – |

Note on step-response cost: the release pane's 10 s window is expensive at full rate. Two options: (a) run the simulation on a `juce::Thread` and publish the result through a double buffer, or (b) let the Mode declare `ballisticsRateHz`, so level-domain Modes can simulate at a decimated rate (for example 4 kHz). Either keeps the message thread under 1 ms per frame.

### 7.4 Rates

| Stream | Rate | Notes |
|---|---|---|
| `UiFrame` publish | per `processBlock` (≈187 Hz at 256 samples / 48 kHz; 23–750 Hz across block sizes) | latest wins; UI reads once per frame |
| `HistoryRing` | 1000 points/s of audio time | burst-delivered per block. At 2048-sample blocks this is a 43 ms jump (4 px at the 5 s span). Optional smoothing: display time = last audio time − 1 block + wall-clock since arrival, clamped |
| UI frame | 60–120 Hz while live audio > −70 dBFS, GR ≠ 0, any easing or hover; 12 Hz idle (HR FramePump) | history column ingestion is independent of the frame rate |
| Static curve, SC curve, colour curve | on snapshot-hash change | ≤1 per frame |
| Step response | on change while TIME is visible, ≤20 Hz during drags | background thread |
| `presentation()` for 15 slots | per frame, cached | – |
| a11y refresh | per frame (HR `:455-476`). Consider 10 Hz for live strings | – |

---

## 8. Accessibility and keyboard

### 8.1 Keyboard (extends HR `keyPressed :1869-1927`)

- **Tab / Shift-Tab walks every operable item** in reading order:
  1. Mode latch
  2. Preset strip items
  3. DELTA, BYPASS
  4. Band tabs
  5. P1…P6, each followed by its attached word
  6. S1…S9, each followed by its attached word
  7. Theme cells

  HR walks only `controls_` (`:1880-1893`), so its Order cells, Freeze and themes cannot be reached from the keyboard.
- Locked, derived and n/a slots **are in the Tab order**, so their reason can be discovered. Focusing one puts the reason on the footer.
- Focus ring: HR's 4 accent hairlines on `hit.reduced(1)` (`:1563-1570`).

| Key | Continuous slot | Stepped slot | Locked / derived / n/a | Latch / tab / Mode latch |
|---|---|---|---|---|
| ↑ / → | +0.01 of the **track** (Mode sub-range), Shift +0.001 | **next detent** (HR's ±0.01 normalised snaps back on stepped params, A §3.7) | no write; footer shows the reason | tabs: next tab; Mode latch: next Mode |
| ↓ / ← | −0.01 / −0.001 | previous detent | same | previous |
| PageUp / PageDown | ±0.1 | ±1 detent | – | – |
| Home / End | track min / max | first / last detent | – | – |
| Delete / Backspace | reset to the Mode default | reset to the Mode default detent | – | – |
| Return / Space | – | – | – | toggle latch / pin tab / open Mode browser |
| Esc | hide ring (HR) | same | same | close browser |

- Every write is a `begin/set/end` triple. Handled keys return `true` so Logic and Live do not swallow them (`:1925`).
- Space may be taken by host transport in some DAWs. Return is the reliable activation key.
- **In the Mode browser:** arrows move a 2-D highlight (↑↓ within a group column, ←→ across columns), Return commits, Esc cancels, letters type-ahead.
- **Replay hook:** `FCMP_UI_KEYS` = HR's `replayKeys` (`:1933-1975`) with `home, end, pageup, pagedown` added. Better, use the Panel's `key()` API (C §3.3).

### 8.2 Accessibility tree (extends HR `AccessibleItem :249-424`)

| Item | Role | Value interface | State | Title / description / help |
|---|---|---|---|---|
| Continuous slot | `slider` | range **{lo, hi} of the Mode track in plain units**, interval = track step. HR uses 0..1 at 0.01 (`:360`). `getCurrentValueAsString` = formatted value and unit, spoken form | – | title = Mode label ("Input"). **`setDescription(aka)`** ("controls threshold"). Help = Mode spec |
| **Stepped slot** | `slider` | **index space: range {0, n−1}, interval 1, current = detent index.** `setValue(i)` writes detent i. The string is `Detent::spoken` ("4 to 1", "all buttons", "0.3 milliseconds") | – | Help: "5 steps: 4, 8, 12, 20, all" |
| Locked | `slider`, **read-only** | `isReadOnly() = true`. Value "10 milliseconds, fixed" | **`setEnabled(false)`** → `isAccessibilityEnabled` NO (`juce_Accessibility_mac.mm:75-78`) | **`setHelpText(reason)`** → `accessibilityHelp` (`juce_AccessibilitySharedCode_mac.mm:211`, `AccessibilityHandler.h:161`) |
| Derived | `slider`, read-only | live derived value ("2.4 seconds, follows time") | enabled (it carries live information) | help = reason |
| N/A | `staticText` | – | `setEnabled(false)` | title "Knee, not applicable", help = reason |
| Attached word, DELTA, BYPASS | `toggleButton` | – | checkable/checked (HR `Handler :365-378`) | – |
| Band tabs, scale and span words, theme cells | `radioButton` | – | checked = active | – |
| Mode latch | `comboBox` | value = Mode name | – | press opens the browser |
| Mode browser rows | `listItem` (HR preset rows); group headings `staticText` | – | selected = current | help = the Mode's spec line |
| Region T, Region H | `image` | – | – | title regenerated at ≤4 Hz: "Transfer curve: threshold −18 dB, ratio 4 to 1, knee 6 dB, gain reduction 3.2 dB" and "History, 5 seconds: peak gain reduction 6.1 dB" |
| Meters | `progressBar` | value "−4.2 dB" | – | – |

- Handles on the curve are **not** separate a11y items. They duplicate the slots.
- The a11y model is produced by `Panel::accessibilityModel()` (C §3.3) and gated **per Mode** by C's G4 golden text files. With the state and help columns added, a Mode that forgets a `reason` fails review in plain text.
- Why index space is required: JUCE's macOS increment calls `setValue(clamp(current + interval))` (`juce_AccessibilitySharedCode_mac.mm:146-160`). With HR's 0..1 / 0.01 range on a stepped parameter, VoiceOver's increment rounds back to the same detent and does nothing.

---

## 9. Ink and colour assignments (one table, both themes by token swap)

| Token | Jobs in FCompressor |
|---|---|
| ground | window, clear colour, overlay fills, handle interiors |
| ink16 | grids, unity diagonal, history IN fill, GR wedge, ghost curves, dotted locked tracks, **n/a labels and values**, footer signature, latch off-fill |
| ink32 | axis labels, IN top stroke, threshold line at rest, locked labels and values, tags, inactive tabs, inactive detent labels, knee bracket, net curve, footer spec line |
| ink52 | slot labels, captions, units, DET trace, derived values, legend names, freeze cursor, peak markers in COLOUR |
| ink70 | OUT trace, static curve at rest, handles, active tab, hovered labels, latch on-fill, IN/OUT RMS bars |
| ink100 | values, op dot, active detent tick and label, Mode name, peak-hold ticks, HOLD phase |
| accent | **the thing under the hand only:** slot value, caret and fill, the curve that control shapes, the dragged handle or threshold line, focus ring, pressed latch |
| accentDim | hover and drag track fill |
| **signal** (renamed `ice`, `#7FD4E8` / `#1E7E96`) | **live gain reduction only:** history GR line and fill, GR meter, GR needle on the curve, big GR number, GR-used bar on RANGE |

---

## 10. Open decisions for the owner

1. **Panel size.** The proposal is 960×640. Keeping 880×520 would leave the band about 150 px tall (HR's own band is y 160–284), which is 3.1 px/dB at the 48 dB scale, and would cramp the rows. 960×640 fits a 1440×900 display.
2. **Characteristics as a band** (recommended) **vs an `EXPAND` overlay** that grows the band over the secondary row. The overlay is disclosure and breaks HR rule 10. Ship without it.
3. **Remap direction.** "Right increases the labelled quantity" (proposed) means INPUT up = threshold param down. The host lane then moves opposite to the knob. The alternative keeps the universal direction and prints descending INPUT values.
4. **AUTO release as a latch** (proposed, one place for all Modes) **vs an SSL/Glue-style `AUTO` detent** on the release rule, which writes two params in one gesture.
5. **Locked params keep their stored raw value** and only stepped params are snapped on a Mode switch. This refines B §7.4.2. Confirm it.
6. **A universal `VOICE` param** (Mode-defined, ≤4 detents). Without it, Mode variants must become separate Modes.
7. **Scale and span persistence:** machine-wide preference (proposed, as Pro-C) or per instance.
8. **`renderStepResponse`** as a shipping DSP API, and whether it runs on a background thread or at a decimated ballistics rate.
9. **Meter semantics:** IN/OUT as peak + RMS (proposed) vs adding a LUFS-M readout (Pro-C 3). A LUFS readout needs a K-weighting path on the audio thread.

---

## 11. Sources

Verified from primary documents: manual PDFs extracted to text, or vendor pages.
- FabFilter Pro-C 3 manual (styles, knee and level displays, meter scale 9–90 dB, knob ranges, Vari-Mu and TTM semantics): https://www.fabfilter.com/downloads/pdf/help/ffproc3-manual.pdf ; displays page: https://www.fabfilter.com/help/pro-c/using/displays
- Sound On Sound, Pro-C 3 review: https://www.soundonsound.com/reviews/fabfilter-pro-c-3
- DMG Audio Compassion manual (main graph, rails, green/red GR, dragging, pause on mouse-over, Knee and Response graphs, Detector X-Y, Curve Law): https://dmgaudio.com/dl/DMGAudio_Compassion_Manual.pdf ; SOS review: https://www.soundonsound.com/reviews/dmg-audio-equality-compassion
- Klanghelm MJUC user guide (MK1/MK2/MK3 panels, TIMING 1–6, VU modes): https://klanghelm.com/docs/MJUC-manual.pdf
- Klanghelm DC8C 2 manual (Easy/Expert, LIMITING inert controls, SOFT/NOSE/SPIKE, program dependency): https://klanghelm.com/docs/DC8C2-manual.pdf
- TDR Kotelnikov manual: https://docs.tokyodawn.net/kotelnikov-manual/ ; TDR Molot GE manual: https://docs.tokyodawn.net/molot-ge-manual/
- Cytomic The Glue manual (stepped values, knee linked to ratio, notched values, needle): https://cytomic.com/files/TheGlue-Manual.pdf
- Sonnox Oxford Dynamics manual (ACCESS sections, permanent transfer display, timing laws): https://dload.sonnoxplugins.com/pub/plugins/manuals/SonnoxDynMan.pdf
- ToneBoosters TB Compressor manual (modes table, Levels tab, draggable threshold, slider assist): https://www.toneboosters.com/manuals/TB_Compressor.pdf
- Softube Weiss DS1-MK3 manual: https://www.softube.com/user-manuals/weiss-ds1-mk3
- Kilohearts Compressor: https://kilohearts.com/products/compressor
- UA 1176 collection tips: https://www.uaudio.com/blogs/ua/1176-collection-tips ; 1176 overview: https://en.wikipedia.org/wiki/1176_Peak_Limiter

From search-result summaries only. Re-verify before a Mode relies on these numbers:
- SSL G bus values (UA SSL 4000 G manual): https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual
- Fairchild 670 time constants: https://www.soundonsound.com/reviews/fairchild-660-670
- LA-2A behaviour: https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual
- Arturia Comp FET-76: https://www.arturia.com/products/software-effects/comp-fet76/overview
- NI Supercharger GT: https://www.native-instruments.com/fileadmin/ni_media/downloads/manuals/SUPERCHARGER_GT_Manual_English_04_2022.pdf
- Waves CLA-76 (1–7 knob scale): https://assets.wavescdn.com/pdf/plugins/cla-76-compressor-limiter.pdf
- Airwindows ships without a custom GUI (general knowledge, not fetched).

Local sources (read-only):
- `HardwareReverb/Source/BgfxEditor.cpp` (1987 lines at read time), `BgfxEditor.h:51-52, 221`, `gui/Theme.h`, `gui/TypeScale.h`, `PresetPanel.h:14-18`, README "GUI" and "The band under the display"
- JUCE 8.0.4 in `HardwareReverb/build/_deps/juce-src/modules/juce_gui_basics/native/accessibility/juce_Accessibility_mac.mm:75-78`, `juce_AccessibilitySharedCode_mac.mm:146-160, 211`, `accessibility/juce_AccessibilityHandler.h:149, 161`
