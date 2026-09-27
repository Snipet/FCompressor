# Mu Mastering (slot 12, `mu-mastering`)

Mode sheet (01 §8.4 step 6): what Mu Mastering runs, every heuristic constant with its source and fit, and the revision
history. Mu Mastering is the fifth Wave 2 Mode (ADR-77, ADR-82), added by the lead without a sprint card. The unit is
D §2.3's Manley Variable Mu in its Mastering version: remote-cutoff tubes in a feedback gain-control chain, a
COMPRESS/LIMIT switch, stepped mastering controls and M/S. The code is `Source/fcdsp/modes/mu-mastering/`.

## Identity

| Field | Value |
|---|---|
| Key, slot | `mu-mastering`, 12 (`Modes.def`, Wave 2) |
| Group, rigor, family | varimu, `Rigor::character`, `CurveFamily::custom` |
| Topology | feedback only (`kTopologies = FB`) |
| Revision | 1 |
| Provisional | no (its own traits are installed; the fidelity rows block and the goldens are blessed) |

## Traits (MuMastering.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `PeakLog` | DETECT locked TUBE |
| Computer | `QuadKnee` (FB) | THRESHOLD 21 steps (−12…−24 dBFS); COMPRESS 1.5:1 / LIMIT 4:1; KNEE derived from the switch |
| Link | `LinkMax` | SEP / LINK (the DC control voltages combined) |
| Ballistics | `SmoothBranching` | ATTACK 20, 25 … 60, 70, 80 ms; RECOVERY 0.2 / 0.4 / 0.6 / 4 / 8 s |
| Stage 2 | `SharedElementMaxT<PeakLog, SmoothBranching, 950, 300>` | LIMIT's rise to 20:1 (s2 slots n/a: part of the switch) |
| Colour | `TubePushPull` | VOICE locked 5670; INPUT drives it |
| SC shape | `Flat` | SC HPF OFF / 100 Hz (the unit's switch, the host's SC HPF); STEREO ST / M/S |

## COMPRESS and LIMIT

- **COMPRESS** is 1.5:1 with a soft knee (D §2.3 [V S12]): QuadKnee in feedback with a 12 dB output knee, which spans
  15 dB at the input. Measured on the element's curve: 1.500:1 at T + 25.
- **LIMIT** is 4:1, and "at greater than 12 dB of limiting the ratio increases (up to 20:1)"; the manual says it behaves
  "like a compressor followed by a limiter" (D §2.3 [V S12]). That is how it is built:
  - stage 1 is QuadKnee 4:1 in feedback with a 3 dB output knee (7.5 dB at the input);
  - stage 2, `SharedElementMaxT`, is a 20:1 section with a 3 dB knee on the same element. It sits kRiseOnsetDb = 3.6 dB
    above the threshold at the output and senses the element's output, as the diode Modes' limiter does;
  - the element takes the larger GR.

  In a hard-knee sketch, the 20:1 section takes over at 3.56 × the onset of GR, about 12.8 dB. The knees round it, and
  the fit measures (`dsp.mumastering`):

| GR | 6 dB | 12.25 dB | 25 dB |
|---|---|---|---|
| local ratio | 4.00:1 | passes 8:1 | 18.8:1 |

`SharedElementMax` became `SharedElementMaxT<Det, Bal, kSlopePermille, kKneeCentiDb>`. `SharedElementMax` is an alias
for 990 / 50, the diode limiter's 100:1 and 0.5 dB; Diode 609 and Diode 54 assert both constants and no golden of
theirs moved.

The rise runs on the side chain's own open-loop times: one side chain, one RC. Its closed loop is therefore faster than
the 4:1 curve's by its larger loop gain, as the unit's is. When it was first given its own ADR-63 conversion, t × 20,
the rise's open-loop attack was 400 ms against an 8 s release. It then settled below the sine's peak-hold value, which
read 7.4:1 against the declared 8.8:1 on `dsp.static`.

## THRESHOLD, INPUT, OUTPUT

- **THRESHOLD:** the Mastering version's 21 steps are "calibrated to LIMIT mode" and span about 12 dB (D §2.3 [V S12]).
  The dial reads 0…20, from −12 to −24 dBFS in 0.6 dB steps; dial 10 is −18 dBFS (0 VU), the default. The unit's
  narrower span in COMPRESS (about 6 dB) is not modelled. The input threshold must move 1:1 with the parameter (K1 #9,
  `dsp.registry`'s thr.slope), because the TRANSFER handle drags it.
- **INPUT** (`drive`, −10…+10 dB [H]) is the input attenuator: a gain into the threshold (preGainDb) and into the tubes
  (driveDb), so "pushing Input and pulling Output gives gentle tube distortion". +6 dB lowers the input threshold by
  exactly 6 dB.
- **Tube trim:** the tubes run kTubeTrimDb = −8 dB under Mu 67's calibration. At 1.5:1 the wet signal is hotter than on
  Mu 67, and TubePushPull at Mu 67's level moved `dsp.time`'s meter truth by 0.2 dB against the 0.1 dB budget. The
  unit's own figure is under 0.1 % THD at 1 kHz; measured at 0 VU with no GR, THD is −142 dB.
- **OUTPUT** is the output attenuator, shown as its dial with unity at −11.5 (the Mastering version). Its 0.5 dB steps
  are continuous here.

## Heuristic constants [H]

| Constant | Value | Where | Source and reason |
|---|---|---|---|
| THRESHOLD steps | −12…−24 dBFS, 0.6 dB | `MuMasteringDesc.cpp` `kThr` | 21 steps over about 12 dB (D §2.3 [V S12]), centred on 0 VU |
| COMPRESS knee | 12 dB output (15 dB input) | `kCompressKneeDb` | "soft knee" |
| LIMIT knee | 3 dB output (7.5 dB input) | `kLimitKneeDb` | fitted: 4.00:1 by 6 dB of GR |
| Rise onset | 3.6 dB over T (output) | `kRiseOnsetDb` | fitted: 8:1 passed at 12.25 dB of GR ("greater than 12 dB") |
| Rise slope, knee | 20:1, 3 dB | `MuMastering::kRiseSlopePermille`, `kRiseKneeCentiDb` | "up to 20:1"; 18.8:1 at 25 dB of GR |
| ATTACK positions | 20…80 ms | `kAtk` | 25–70 ms "slightly extended in both directions", 11 positions [U values] |
| INPUT range | ±10 dB | `Pid::drive` | the attenuator's working range |
| Tube trim | −8 dB | `kTubeTrimDb` | meter truth; under 0.1 % THD |

## Verification

| Probe | What it holds Mu Mastering to |
|---|---|
| `dsp.mumastering` | COMPRESS 1.5:1 with no rise; LIMIT 4:1 at moderate GR, 8:1 passed at 10–15 dB of GR, ≥ 15:1 at 25 dB; INPUT's 1:1 threshold shift; THD under −60 dB at 0 VU |
| `dsp.static.mu-mastering` | the measured curve against the element's declared curve (with its settled stage 2: dsp.static's v1.2 change, the identity for every other Mode) |
| `dsp.time`, `dsp.zipper`, `dsp.switch`, `dsp.srsweep`, `dsp.analysis`, `dsp.hostile`, `dsp.link`, `dsp.null`, `dsp.print`, `dsp.rt`, `dsp.latency` | the generic per-Mode rows |

## Known limits

- The narrower COMPRESS threshold span is not modelled.
- The T-Bar tube option is not modelled.
- In LIMIT, the rise's detector is a second loop on the same side chain, not one physical curve. The static curves are
  the same, but under program the rise's loop charges on its own, the documented residual of `SharedElementMax`.

## Revision history

| Revision | Release | Change |
|---|---|---|
| 1 | v1.2 (unreleased) | First version. Not shipped yet (no `modes-ever.tsv` row until the release). |
