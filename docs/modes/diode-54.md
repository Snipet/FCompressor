# Diode 54 (slot 11, `diode-54`)

Mode sheet (01 §8.4 step 6): what Diode 54 runs, every heuristic constant with its source, and the revision history.
Diode 54 is the fourth Wave 2 Mode (ADR-77, ADR-80), added by the lead without a sprint card. The unit is D §2.5's Neve
2254 (/A, /E, /R): a compressor and a limiter, both feedback, sharing one diode-bridge gain element, with four
transformers and discrete class-A stages. The engine is **Diode 609's** (docs/modes/diode-609.md, the 33609 of the
same lineage), so this sheet covers what the 2254 changes. The code is `Source/fcdsp/modes/diode-54/`.

## Identity

| Field | Value |
|---|---|
| Key, slot | `diode-54`, 11 (`Modes.def`, Wave 2) |
| Group, rigor, family | diode, `Rigor::character`, `CurveFamily::custom` |
| Topology | feedback only (`kTopologies = FB`) |
| Revision | 1 |
| Provisional | no (its own traits are installed; the fidelity rows block and the goldens are blessed) |

## Traits (Diode54.h), against Diode 609

| Slot | Policy | Diode 54 (2254) | Diode 609 (33609) |
|---|---|---|---|
| Detector | `PeakLog` | the same | |
| Computer | `QuadKnee` (FB) | THRESHOLD −20…+10 dBu, 2 dB; RATIO 1.5/2/3/4/6, default **3:1**; the 10 dB knee from RATIO | the same, default 2:1 |
| Link | `LinkMax` | DUAL / STEREO ("outputs of both side-chains are combined") | the same |
| Ballistics | `AutoSwitch<SmoothBranching, DualReleaseT<1,2,3>>` | ATTACK **5 MS fixed or FAST 0.1–2 ms** (a hybrid); RECOVERY **.1 / .2 / .8 S + AUTO** | FAST / SLOW 3 / 6 ms; .1 .4 .8 1.5 S + A1 / A2 |
| Stage 2 | `SharedElementMax<PeakLog, AutoSwitch<…, DualReleaseT<4,5,6>>>` | LIMIT THRESHOLD **+4…+20 dBu, 2 dB** / OFF; LIMIT ATTACK as ATTACK; LIMIT RECOVERY .1/.2/.8 S + AUTO | +4…+15 dBu, 1 dB; FAST / SLOW; 50…800 ms + A1/A2 |
| Colour | `DiodeBridge` | VOICE locked DIODE (+ DRIVE) | the same |
| SC shape | `Flat` | **no SLOW high-pass** | SlowHp (ATTACK SLOW) |

GAIN is 0–20 dB in 2 dB steps (D §2.5 [V S11]). The dBu law is 0 dBFS = +22 dBu (01 §3.1).

## ATTACK: the first hybrid

The 2254's attack is "fixed 5 ms. An optional fast attack is adjustable from 100 µs to 2 ms" (D §2.5 [V S11], the
/R). That is a `Kind::hybrid` parameter: continuous from 0.1 to 2 ms (the FAST pot), plus one step outside that range,
5 MS (the default). The panel shows the pot's range with the 5 MS end cell. The Characteristics step plot drags the
marker on the time axis inside the range; the step is chosen with the end cell. LIMIT ATTACK is the same switch on the
limiter's side chain.

Both attacks are closed-loop times (ADR-63), converted in `physical()` to each loop's open-loop τ = t (1 + k), as in
Diode 609. Measured (`dsp.diode54`, `dsp.time`): 5 MS gives 5.007 ms; FAST 0.1 / 0.447 / 2 ms gives 0.107 / 0.454 /
2.007 ms.

Diode 54 is the first Mode with a hybrid, and putting one on screen needed two editor fixes (ADR-80):
- `SlotModel::plotToHost01` lets a hybrid's time marker drag on the axis. It used to fall back to the relative drag.
- `StepPlot::accessibility` rewrites the handle's value interface on the slider's view state, as SlotGrid does. A
  hybrid on its step is continuous there, so the handle matches the panel slot.

## RECOVERY AUTO

The 2254 lists "Recovery 100, 200, 800 ms + Auto" for both stages (D §2.5 [V S11]); a secondary source's 400 / 800 /
1500 is [C]. AUTO is DualRelease, as Diode 609's A1/A2 are: a fast recovery of 100 ms, and after sustained program the
slow path, which charges in 0.5 s. Its published range is 0.1–1.5 s [H]. τ_Rs is 1.35 s, so that the cascade's 1/e
time after sustained program (τ_Rs plus about τ_Rf, Bus G's lesson) lands inside 1.5 s.

Measured to 1/e: 0.100 s after a 30 ms burst and 1.452 s after 3 s of program. The 2254's "attack time is adjusted
automatically along with the release" in AUTO is not modelled: AUTO keeps ATTACK's time.

## Limiter

LIMIT THRESHOLD runs +4…+20 dBu in 2 dB steps, then OFF (kS2Off; D §2.5 [V S11]). The 33609's runs +4…+15. The limiter
is SharedElementMax's (> 100:1, [C]), max-combined with the compressor on the element. Measured on the element's
static curve (compressor +10 dBu at 1.5:1, limiter +12 dBu, a −4 dBFS level): 6.00 dB of GR against the compressor's
own 2.67 dB. The output sits at −10.00 dBFS, the limiter's threshold.

## Heuristic constants [H]

| Constant | Value | Where | Source and reason |
|---|---|---|---|
| AUTO fast / slow / charge | 100 ms / 1.35 s / 0.5 s | `Diode54Desc.cpp` `kAutoFastMs`, `kAutoSlowMs`, `kAutoChargeMs` | a published range of 0.1–1.5 s; the charge is Diode 609's |
| Knee input span | 10 dB | `kKneeInputSpanDb` | "fairly soft … over a 10dB range" (D §2.5 [V S11]), Diode 609's law |
| Defaults | +4 dBu, 3:1, 5 MS, 0.2 S, GAIN 0, STEREO, LIMIT OFF | the table | 0 VU threshold; the 2254's middle ratio |

## Verification

| Probe | What it holds Diode 54 to |
|---|---|
| `dsp.diode54` | the fixed 5 MS attack (± 0.5 ms), FAST at 0.1 ms (≤ 0.05 × it), AUTO's program dependence (0.1 s after a hit, 0.8–1.5 s after sustained program), the limiter's +20 dBu step and its ceiling on the element |
| `dsp.time.diode-54` | the FAST range's attacks, every RECOVERY position, AUTO in its program range |
| `ui.chars`, `ui.charscreen` | the hybrid marker's drag and its handle's a11y (the editor fixes above) |
| `dsp.static`, `dsp.zipper`, `dsp.switch`, `dsp.srsweep`, `dsp.analysis`, `dsp.hostile`, `dsp.link`, `dsp.null`, `dsp.print`, `dsp.rt`, `dsp.latency` | the generic per-Mode rows |

## Known limits

- AUTO's attack adjustment is not modelled.
- The 2254/E's and /A's differences are not modelled.
- The limiter ratio is the shared element's (> 100:1), not a measured one.

## Revision history

| Revision | Release | Change |
|---|---|---|
| 1 | v1.2 (unreleased) | First version. Not shipped yet (no `modes-ever.tsv` row until the release). |
