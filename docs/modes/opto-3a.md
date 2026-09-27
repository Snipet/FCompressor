# Opto 3A (slot 10, `opto-3a`)

Mode sheet (01 §8.4 step 6): what Opto 3A runs, every heuristic constant with its source and fit, and the revision
history. Opto 3A is the third Wave 2 Mode (ADR-77, ADR-79), added by the lead without a sprint card. The unit is
D §2.2's UREI LA-3A: a T4 opto cell in a class-A solid-state circuit with transformers. The catalogue calls it "a
cheap variant of M02", and that is how it is built: **Opto 2A's cell model, unchanged** (docs/modes/opto-2a.md), with
the LA-3A's constants, a lighter voice and the rear HF pot. The code is `Source/fcdsp/modes/opto-3a/`.

## Identity

| Field | Value |
|---|---|
| Key, slot | `opto-3a`, 10 (`Modes.def`, Wave 2) |
| Group, rigor, family | opto, `Rigor::character`, `CurveFamily::custom` |
| Topology | feedback only (`kTopologies = FB`); an external key runs the curve feed-forward (E §2.6) |
| Revision | 1 |
| Provisional | no (its own traits are installed; the fidelity rows block and the goldens are blessed) |

## Traits (Opto3A.h), against Opto 2A

| Slot | Policy | Opto 3A | Opto 2A |
|---|---|---|---|
| Detector | `OptoSense` | the same | |
| Computer | `FeedbackDelayed<OptoCellCurve>` | COMP 3:1 (k 2) / **LIMIT 4:1 (k 3)** | COMP 3:1 / LIMIT 10:1 |
| Link | `LinkMax` | DUAL / LINK | the same |
| Ballistics | `OptoCell` | attack **1.5 ms** (program), 60 ms to 50 %, memory **charge 2 s, release 2 s** | 10 ms; 5 s, 5 s |
| Colour | `ClassATransformer` (`TubeTransformerT<40>`) | VOICE locked **CLASS A + TRANSFORMERS**, even term 0.04 | TUBE, 0.1 |
| SC shape | `R37Shelf` | **HF SENS** 0–10 (the rear pot) | EMPHASIS 0–10 (R37) |

The PEAK RED., GAIN and HF SENS dials are Opto 2A's laws. PEAK RED. runs 0…100 ↔ +20…−50 dBFS with a default of 40
(−8 dBFS); GAIN runs −10…+30 dB with a default of +6. `m[]` uses the same slots: 0 shelf, 1 LIMIT flag, 2–4 β, τ_m, τ_s.

## What the LA-3A changes

**Attack.** It is "1.5 ms or less, program dependent" (D §2.2 [V S32]). The locked nominal is 1.5 ms, and the kit's
program band gives 0.75–3 ms. Opto 2A's loop fit is kept: τ_on = published × (1 + k), ADR-63. Measured: 1.60 ms at
COMP, against Opto 2A's 10.7 ms.

**Release.** Stage 1 is 60 ms to 50 %, as on the LA-2A; the same fit gives 60.0 ms measured. After that the release is
"quick with occasional light compression … slower when driven hard continuously" (D §2.2 [V~ S32]). The memory,
OptoCell's slow part, charges only while the light is on. A short passage therefore leaves little of it, and a long,
deep one leaves β = ½ of the GR, which then lets go at τ_s. Measured to 0.5 dB of GR after a burst at T + 20:

| Burst | 0.1 s | 10 s |
|---|---|---|
| Opto 3A | 0.26 s | 5.4 s |
| Opto 2A | 0.26 s | 12.8 s |

**COMP / LIMIT.** The two "sound virtually indistinguishable unless very heavy compression is used" (D §2.2 [V S32]).
OptoCellCurve ties the knee to the exponent k, so the switch cannot keep the curve and change only its end. LIMIT is
therefore 4:1 (k 3) [H], against Opto 2A's 10:1. The static GR, COMP against LIMIT:

| x − T (dB) | 0 | +10 | +30 |
|---|---|---|---|
| COMP | 3.32 | 8.11 | 20.30 |
| LIMIT | 2.80 | 8.52 | 22.67 |

The two stay within 0.5 dB up to moderate GR and part by 2.4 dB under heavy GR. The one search summary that puts the
LA-3A's limit near 50:1 is [U], and it contradicts the "indistinguishable" statement.

**HF SENS.** The rear pot sets the side chain's HF sensitivity (D §2.2 [V S32]). It uses R37Shelf, the same first-order
shelf as Opto 2A's EMPHASIS: unity above 1 kHz, down to −10 dB below 316 Hz at the pot's end. The cell hears the top
end up to 10 dB louder than the lows, so bright material leads the GR. Measured at HF SENS 10: −9.63 dB at 100 Hz (the
shelf's own value there) and −0.03 dB at 10 kHz.

**Voice.** The LA-3A is class-A solid state with transformers, "brighter and faster" than the LA-2A ([U]).
TubeTransformer became `TubeTransformerT<kEvenPermille>`:
- `TubeTransformer` = `<100>` for Opto 2A. It is bit-identical, a `static_assert` holds kEven at 0.1f, and no Opto 2A
  golden moved.
- `ClassATransformer` = `<40>` for Opto 3A. It keeps the same transformer tanh and the same kIn, so the meter-truth
  budget is Opto 2A's (docs/modes/opto-2a.md "Meter truth"), with 0.4 × the even term.

Measured at a 0 dBFS sine: H2 −70.1 dB against −62.2 dB, which is 20 log10(0.4) = −7.96 dB; H3 is the same at
−93.8 dB. DRIVE (±24 dB, the extension) scales both.

## Heuristic constants [H]

| Constant | Value | Where | Source and fit |
|---|---|---|---|
| Attack nominal | 1.5 ms | `Opto3ADesc.cpp` ATTACK | "1.5 ms or less" (D §2.2 [V S32]) |
| LIMIT ratio | 4:1 (S 0.75, k 3) | `kRatio` | "virtually indistinguishable unless very heavy compression" |
| τ_m (memory charge) | 2 s | `kMemoryChargeMs` → `m[3]` | only continuous GR builds the memory (Opto 2A 5 s) |
| τ_s (slow release) | 2 s | `kSlowReleaseMs` → `m[4]` | "quick", with a tail of a few seconds after continuous heavy GR (Opto 2A 5 s) |
| β, attack loop, release fit | ½, 1.0, 0.85 | as Opto 2A | the same cell and panel |
| Even term | 0.04 | `ClassATransformer` | a class-A stage's second harmonic, below the tube's |
| Tail | 10 s (5 τ_s) | `opto3aTail` | the memory's tail |

## Verification

| Probe | What it holds Opto 3A to |
|---|---|
| `dsp.opto3a` | the attack (0.75–3 ms, ≤ 0.2 × Opto 2A's), the memory (quick after 0.1 s, 2–8 s after 10 s, ≤ 0.6 × Opto 2A's), COMP/LIMIT (within 1 dB to T + 10, ≥ 1.5 dB apart at T + 30), HF SENS (the shelf's value at 100 Hz, flat at 10 kHz), the voice against Opto 2A's (H2 −7.96 dB, H3 equal) |
| `dsp.time.opto-3a` | attack 1.60 ms in 0.75–3 ms; release 60.0 ms to 50 % in 40–80 ms |
| `dsp.static`, `dsp.zipper`, `dsp.switch`, `dsp.srsweep`, `dsp.analysis`, `dsp.hostile`, `dsp.link`, `dsp.null`, `dsp.print`, `dsp.rt`, `dsp.latency` | the generic per-Mode rows |

## Known limits

- Not modelled: the reissue's −20 dB input pad (PEAK RED.'s range covers it) and the meter's OUTPUT position.
- The LA-3A's own THD is [U]. The even term is set against Opto 2A's and within meter truth, not measured.

## Revision history

| Revision | Release | Change |
|---|---|---|
| 1 | v1.2 (unreleased) | First version. Not shipped yet (no `modes-ever.tsv` row until the release). |
