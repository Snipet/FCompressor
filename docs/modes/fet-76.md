# FET 76 (slot 2, `fet-76`)

Mode sheet (01 §8.4 step 6): what FET 76 runs, every heuristic constant with its source and reason, and the revision
history. Owned by the Mode's card (M2 in S9). The contracts are 01 §10.5 (descriptor, traits), 01 §5.2–5.3 (policies,
engine, the feedback step) and ADR-25 / ADR-63; the code is `Source/fcdsp/modes/fet-76/`,
`Source/fcdsp/engine/stages/law/FetVcr.h` and `Source/fcdsp/engine/stages/colour/FetColour.h`. The unit is D §2.1's
UREI/UA 1176 (rev A … H, LN), a feedback FET peak limiter whose INPUT drives a fixed threshold.

## Identity

| Field | Value |
|---|---|
| Key, slot | `fet-76`, 2 (`Modes.def`) |
| Group, rigor, family | fet, `Rigor::character`, `CurveFamily::custom` |
| Topology | feedback only (`kTopologies = FB`); an external key evaluates the computer feed-forward on the key with the loop's open-loop times (E §2.6: the loop is open) |
| Revision | 1 |
| Provisional | no (cleared by M2, S9: its fidelity rows are blocking spec rows and its goldens can be blessed) |

## Traits (Fet76.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `PeakLog` | DETECT locked PEAK · FEEDBACK (the sidechain samples the signal after the gain element, D §2.1 [V S2]) |
| Computer | `QuadKnee` (FB closed form, E §2.6) + `law::FetVcr` | INPUT 0–48 into the fixed threshold kT0, RATIO 4 / 8 / 12 / 20 / ALL, KNEE derived from RATIO; the law maps the loop's GR to the element's resistance, gate CV and divider share |
| Link | `LinkMax` | LINK DUAL / LINK (the 1176SA pair), applied after the per-lane solve (K2 #5b) |
| Ballistics | `SmoothBranching` | ATTACK 20–800 µs, RELEASE 50–1100 ms, both hardware-reversed (fastest clockwise) |
| Stage 2 | `NoStage2` | – |
| Colour | `ColourSelect<FetColour>` | VOICE LN / A / F (the revision's constants); INPUT is the drive (DRIVE n/a) |
| SC shape | `Flat` | (the host's SC HPF, an extension, acts before it) |

GR switch (Q9, ADR-25): the attack knob's OFF detent is `tmode`, relabelled GR, ON = 0 and OFF = 7 (boundary 3.5), so
every other Mode's `tmode` (0 or 1) resolves to ON and `crossmode.no_off` stays clean. OFF sets `kEngGrOff`: the
applied GR ramps to exactly 0 over 20 ms with ModeEngine's smootherstep (`offAmt_`) and never steps; the audio still
passes the colour stage, whose FET part vanishes with the GR (the channel pinched off) while the amplifiers remain
("no gain reduction, the audio still passes the transformers", D §2.1).

## INPUT, the fixed threshold and the dial law (fitted)

The host `thr` is the effective input threshold at 4:1 (01 §3.1: `thr` always means the threshold referred to the
plugin input). physical() turns it into the INPUT knob's gain into a fixed detector-domain threshold:

    preGainDb = kT0 - thr          (INPUT: the gain into the FET stage; it also drives the colour)
    thrDb     = kT0 + offset(ratio)

So the amount of compression carries across Mode switches, and |dT_in/dthr − 1| = 0 (`dsp.registry thr.slope`).
`dsp.fetcolour` holds the gain element's output at T + 10 dB to the same level at INPUT 12, 24 and 36 (1e-3 dB).

- **kT0 = −18 dBFS** (01 §10.5 had −12 [H]). The fixed internal threshold at 4:1 is the operating level at unity
  INPUT: 0 VU = +4 dBu = −18 dBFS (ADR-59, the calibration Bus G's dial zero uses). The INPUT knob's unity mark is 24
  ("unity is about 24 on both Input and Output at 12 o'clock", D §2.1 [V S2]), so INPUT 24 is both unity gain and the
  host default threshold (−18 dBFS, Clean's default): a fresh FET 76, and a switch from Clean's default, read INPUT 24 /
  OUTPUT 24.
- **Dial law: 0.9375 dB per dial unit, linear in dB, symmetric about 24** (01 §10.5 had 1 dB per unit [H]). INPUT
  and OUTPUT are both 0–48 dials with unity at 24, and the unit's maximum gain, both dials at 48, is 45 dB (D §2.1
  [V S2]: "Gain is 45 dB ±1"). So each dial's upper half spans 22.5 dB: 45 / 48 dB per unit. INPUT spans −22.5 …
  +22.5 dB of gain (`thr` = kT0 ∓ 22.5 = −40.5 … +4.5 dBFS), OUTPUT −22.5 … +22.5 dB. The real knobs are ladder
  attenuators that reach −∞ at 0; the linear law below 24 is the symmetric extension (a known limit).

`dsp.fetcolour.mode.dial.*` holds INPUT 24 = unity = the threshold, INPUT 0 / 48 = ∓22.5 dB and INPUT 48 + OUTPUT 48 =
45 dB.

## RATIO, the threshold offsets and the knee

| Button | S (plain) | loop gain k | knee W (output) | FB knee in input dB | threshold offset | measured (T + 25 … T + 35) |
|---|---|---|---|---|---|---|
| 4 | 0.75 | 3 | 8 dB | T − 4 … T + 16 | 0 dB | 4.00:1 |
| 8 | 0.875 | 7 | 5 dB | T − 2.5 … T + 20 | +2 dB | 8.00:1 |
| 12 | 0.91667 | 11 | 4 dB | T − 2 … T + 24 | +4 dB | 12.0:1 |
| 20 | 0.95 | 19 | 2 dB | T − 1 … T + 20 | +6 dB | 20.0:1 |
| ALL | 1.0 | 99 (clamped) | 2 dB | T − 1 … T + 100 | +4 dB | a plateau (below) |

- The knee is the FB curve's, defined at the output (E §2.6): 4:1 softer … 20:1 near-hard (D §5.3) [H]; its
  dependence on anything else is unpublished (D §8.1).
- "Higher ratio settings also set the threshold higher" (D §2.1 [V S2]): offsets 0 / 2 / 4 / 6 dB [H] (01 §10.5); the
  0.5 dB-of-GR onset measures −19.86 / −17.15 / −14.89 / −12.17 dBFS at INPUT 24 (`fetcolour.mode.ratio.onset_order`).
- **ALL** (all buttons, `kTagAll`), a monotone curve (K2 #5a: never a negative slope; `dsp.registry fb.monotone`,
  `fetcolour.mode.all.nonmonotone`) made of: a larger loop gain (S = 1: k = 99, the plateau: from T + 10 to T + 30 the
  output rises 0.45 dB against 20:1's 1.08 dB), its own threshold offset (+4 dB, between 8 and 20 [H]), the near-hard
  knee, an attack **kAllAttackLag = 3 ×** slower [H] ("a lag time on the attack" lets transients through, D §2.1;
  measured 618 / 203 µs = 3.05 × at the 200 µs knob) and more colour (FetColour, below).

## Time constants (ADR-63)

The descriptor's TimeSpec is the **published closed-loop** time. physical() converts the attack to the loop's
open-loop τ, `atkTauMs = τ_published × (1 + k)`: in the FB loop's linear region the closed loop moves 1 + k times
faster than its one-pole (E §2.6: pole α / (1 + (1 − α) k)). The published attack (`fetAttackSpec`) is the knob, raised
to **40 µs while LINKed** (D §2.1 [V S2]: "the fastest attack time is doubled … 40 microseconds instead of 20") and
slowed ×3 by ALL. The release is not converted: measured the hardware way (the level drops below the threshold), it
runs with the loop open, i.e. at the one-pole's τ.

Measured through the engine (`dsp.fetcolour`, DUAL, D2's t63 on the tapped GR, T − 20 → T + 20 dB):

| Knob | 192 kHz, 4:1 … 20:1 | 48 kHz, 4:1 … 20:1 |
|---|---|---|
| 20 µs | 22.0 … 22.5 µs (3.8 samples) | 28 … 30 µs (under one sample: not judged) |
| 100 µs | 102.0 … 102.5 µs | 108 … 110 µs |
| 800 µs | 802 … 803 µs | 808 … 810 µs |
| release 400 ms | – | 0.400 s at every ratio |

Within 15 % wherever the published time spans ≥ 3 samples (the discrete loop's excess is O(1/N)); `dsp.srsweep` holds
the default attack to 48 kHz's across 44.1–192 kHz within 2.8 % (character: 5 %).

**Stability at the fastest attack** (K2 #1): the zero-delay solve (QuadKnee::solveFb inside SmoothBranching) has the
effective pole α / (1 + (1 − α) k) ∈ (0, α), so it never rings. `fetcolour.mode.stability.*` steps DUAL / 20 µs by 50
dB at 22.05, 44.1, 48, 96 and 192 kHz on every ratio: no overshoot, no reversal, a settled GR still to 1e-3 dB. E §2.6's
naive one-sample loop at 4:1 / 20 µs / 48 kHz reproduces its Nyquist buzz there (24.88 / 8.78 dB), which the same
metric reads as ≥ 10 dB peak-to-peak.

## law::FetVcr [H]

The element is a divider: R_s in series, the FET's channel R_ds to ground; g = R_ds / (R_s + R_ds),
r = 20 log10(1 + R_s / R_ds). With the 1176's half-drain gate feedback the channel conductance is linear in the gate
overdrive, 1 / R_ds = (v / kCvFullV) / R_on. The loop computes r (QuadKnee, in dB); the law turns it back into the
element's quantities (it adds no second curve):

| Constant | Value | Reason |
|---|---|---|
| R_s | 47 kΩ | the attenuator side of the divider (tens of kΩ) |
| R_on | 470 Ω | a small-signal JFET's on-resistance (a few hundred Ω): a 100:1 divider, a 40.09 dB element |
| kCvFullV | 10 V | the LOOP CV that turns the channel fully on (the readout's full scale) |
| kOpenKohm | 100 kΩ | the FET R readout's full scale (the channel pinched off) |

R_ds at 6 / 20 / 40 dB of GR: 47.2 / 5.22 / 0.475 kΩ; LOOP CV 0.10 / 0.90 / 9.90 V. The engine does not clamp its GR at
the element's depth (FET 76's range is n/a); beyond it the readouts saturate.

## FetColour: the revisions [H]

The colour stage runs on the gain element's output (the wet signal, the voltage across the channel) and adds a
memoryless residual of two bounded basis shapes of u = x / 2: an even `tanh²(u)` (second harmonic; never moves a sine's
fundamental) and an odd `tanh(u) − u` (third harmonic and a soft compression), with per-sample weights

    E = s(GR) ((1 − a) e_F + a e_FA) + e_O,     O = s(GR) ((1 − a) o_F + a o_FA) + o_O,     s(GR) = 1 − 10^(−GR/20)

s(GR) is law::FetVcr's share of the divider: the FET's own residual follows the gain reduction and vanishes without it
(and under GR OFF); `a` is the ALL amount (smoothed 20 ms per tick, interpolated per sample). Given as x-domain Taylor
coefficients (x² and −x³):

| VOICE | Revision | FET even | FET odd | output even | output odd | limiting THD (−14 dBFS, 15 dB GR) | no GR: H2 / H3 (−14 dBFS) |
|---|---|---|---|---|---|---|---|
| LN (default) | REV D/E LN: low-noise circuit, class-A output (the reissue's basis) | 0.010 | 0.006 | 0.002 | 0.0009 | H2 −59.9, H3 −84.7 dB: 0.10 % | −74.0 / −100.9 dB |
| A | REV A (blue stripe): no LN, FET preamp, class-A + UA-5002 | 0.050 | 0.012 | 0.020 | 0.004 | H2 −44.3, H3 −77.2 dB: 0.61 % | −54.0 / −88.0 dB |
| F | REV F/H: LN, push-pull output (symmetric) | 0.010 | 0.006 | 0.001 | 0.003 | H2 −60.8, H3 −82.1 dB: 0.092 % | −80.1 / −90.5 dB |
| ALL | FET even × kAllEven = 8, odd × kAllOdd = 3 (the shifted bias points) | | | | | LN + ALL: H2 −43.4 dB, 0.67 % | |

Sources and budgets:
- D §2.1 [V S2]: "within 0.5 % THD 50 Hz–15 kHz with limiting" for the LN: the LN limits at 0.10 % (`fetcolour.colour.ln.thd_limiting_pct` ≤ 0.5 %); "no LN, so more THD" for Rev A (+15.5 dB, row ≥ +6); "push-pull output stage" for F/H (H2 without GR 6 dB under the LN's, row); "distortion increases radically" in ALL (+16.4 dB of H2, row ≥ +12).
- The default voice is held by the meter-truth rows of a character Mode (`tap_vs_audio` ≤ 0.10 dB): per sample on
  dsp.time's 40 dB square steps (0.063 dB at these coefficients) and, under GR OFF with a +18 dBFS wet signal,
  dsp.static's `tmode.off` row (0.064 dB): that bounds the LN's odd output coefficient (≤ ~0.001) and its even
  coefficients. The harmonic analysis row `analysis.harm.*.vs_dft_db` (0.01 dB above −100 dB) keeps every revision's
  harmonics at 0 dB of GR (amp 0.5) out of the −100 … −90 dB band where the float DFTs of two sine tables disagree.
- The even basis is evaluated pointwise, the odd one through ADAA-1 (FetColour.h): ADAA-1's half-sample residual carry
  after a 40 dB level drop read 0.6 dB on the per-sample meter-truth row; a pointwise square law aliases only the
  second harmonic of content above fs/4 at ECO, nothing at STD/HQ (where the colour runs at 2× / 4×).
- No DC blocker: the even residual's level-following DC is part of the static shape (a blocker's tail after a level
  drop read 0.7 dB on the same row, and broke `dsp.hostile`'s exact silence). An exactly silent input sample gives an
  exactly silent output sample.

## Internals (UiFrame, 01 §10.5)

At the applied GR of the louder lane (the ballistics' GR × the GR switch's shaped ramp):

| Word | Meaning |
|---|---|
| LOOP CV (V, the history internal) | law::FetVcr::cvVolts |
| FET R (kΩ) | law::FetVcr::resistanceKohm (100 = pinched off) |
| H2, H3 (dB) | FetColour::harmonicsEstimate (third-order) of a sine at the static FB curve's output level for that GR (T + r/k above the knee, T − W/2 + √(2Wr/k) inside, T − W/2 without GR); the COLOUR view draws the exact DFT |

## Verification

| Probe | What it holds FET 76 to |
|---|---|
| `dsp.fetcolour` | law::FetVcr (round trip, share, depth, monotone), FetColour (share scaling, process = transfer, Taylor estimate vs DFT, the revision and ALL figures above, silence, block-size invariance, poison, ALL smoothing), the dial law, INPUT's fixed threshold, the measured ratios and onsets, ALL (monotone, plateau, lag), closed-loop attack / open release against the published times, LINK's 40 µs, the GR switch (resolve, ramp, lands, colour under OFF), stability at 20 µs at 22.05–192 kHz, the internals |
| `dsp.static.fet-76` | D1 for every ratio at two INPUT settings (the FB curve against the declared staticGr, the FB bisection rows ≤ 1e-5 dB), every voice's describing-function curve, the GR switch's curve |
| `dsp.time.fet-76` | the attack and release range ends and middle, measured (NOTE lines: a character Mode's times are judged against the published ones in `dsp.fetcolour`), tap against audio per sample, the GR OFF ramp's click metric, the carry, the telemetry, τ across ECO/STD/HQ |
| `dsp.srsweep`, `dsp.link`, `dsp.analysis`, `dsp.hostile`, `dsp.null`, `dsp.switch` (FET 76 ↔ Clean at mix 0.5), `dsp.zipper`, `dsp.quant`, `dsp.rt`, `dsp.print`, `dsp.latency`, `dsp.registry` (`crossmode.no_off`, `fb.monotone`, `thr.slope`) | the generic per-Mode rows, fidelity rows blocking |

`dsp.zipper`'s detent renders floor the attack at 50 ms so a detent edge reads the transition, not the compressor's
reaction; for a feedback kernel whose detents change the static curve the floor is now on the closed-loop attack
(× (1 + k), ADR-63) — M2's change to that probe, for the lead's approval.

## Known limits

- The release is not program-dependent (D §2.1 [U]: magnitude unpublished); the ALL curve does not "shift
  continually" and has no overshoot plateau beyond what the lagged attack lets through.
- The transformers' LF coupling and the rise of distortion at low frequencies are not modelled (the colour is
  memoryless); the waveform-following GR of a fast release at low frequencies is (the loop's own ripple).
- 20 µs at 44.1/48 kHz is under one sample: the closed loop reaches ~28–30 µs there (a ZDF loop cannot be faster than
  its sample period); STD/HQ do not change the control rate.
- `EngineTelemetry::attackNowMs` reports the loop's open-loop τ (the ballistics' own time, ADR-63); FET 76's attack
  is not program-dependent, so no readout shows it.
- FB sub-ulp carry (ADR-66): until S10 the feedback kernel dropped a slow release's sub-ulp steps and stalled up to
  ulp(r) / (2c) short of a non-zero target (about 0.025 dB for the 1.1 s release at 48 kHz and a target of 8–16 dB).
  The S10 interface revision (X10, `FbAffine::base`) makes SmoothBranching's FB step carry its remainder as the FF step
  does, so a release follows the exact discrete FB recurrence. This moved `dsp.print` `default` and `lo` (their
  releases, 400 ms and 108 ms, carry at 48 kHz) by at most 0.0025 dB of gain; `hi` and `mid` are unchanged. Not
  shipped, so no revision bump. `dsp.srsweep`'s long-release row stays a NOTE for FB kernels.

## Revision history

| Revision | Sprint | Change |
|---|---|---|
| 1 | S9 (M2) | Full traits (01 §10.5), provisional cleared; [H] constants fitted (kT0 −18 dBFS, dial 0.9375 dB/unit), ADR-63 attack conversion, FetColour and law::FetVcr. Not shipped (no `modes-ever.tsv` row), so no bump. |
