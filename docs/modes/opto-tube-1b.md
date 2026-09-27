# Opto Tube 1B (slot 13, `opto-tube-1b`)

Mode sheet (01 §8.4 step 6): what Opto Tube 1B runs, every heuristic constant with its source and fit, and the revision
history. Opto Tube 1B is the sixth and last Wave 2 Mode (ADR-77, ADR-83), added by the lead without a sprint card. The
unit is D §2.2's Tube-Tech CL 1B, an optical compressor with a tube amplifier and a real threshold and ratio, and its
Mk II's side-chain low cut and mix. The code is `Source/fcdsp/modes/opto-tube-1b/`.

## Identity

| Field | Value |
|---|---|
| Key, slot | `opto-tube-1b`, 13 (`Modes.def`, Wave 2) |
| Group, rigor, family | opto, `Rigor::character`, `CurveFamily::textbook` |
| Topology | feed-forward (`kTopologies = FF`; the unit's is [U]) |
| Revision | 1 |
| Provisional | no (its own traits are installed; the fidelity rows block and the goldens are blessed) |

## Traits (OptoTube1B.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `OptoSense` | DETECT locked OPTO: the optical cell's light, Opto 2A's sensor (5 ms hold, 5 ms afterglow) |
| Computer | `QuadKnee` | THRESHOLD +20…−40 dB + OFF (a hybrid: −58…+2 dBFS continuous, OFF at +24); RATIO 2…10:1 continuous; KNEE locked 10 dB |
| Link | `LinkMax` | DUAL / LINK (the side-chain bus) |
| Ballistics | `AutoSwitch<SmoothBranching, DualReleaseT<0,1,2>, FixManSelect>` | A/R SELECT MANUAL / FIXED / FIX-MAN (below) |
| Stage 2 | `NoStage2` | – |
| Colour | `TubeTransformer` | VOICE locked TUBE, 12 dB under Opto 2A's drive; DRIVE the extension |
| SC shape | `Flat` | SC LOW CUT (the Mk II's; the host's SC HPF); MIX native (the Mk II's) |

## ATTACK/RELEASE SELECT

| Position | `tmode` | Attack | Release |
|---|---|---|---|
| MANUAL | 0 | ATTACK 0.5–300 ms | RELEASE 0.05–10 s |
| FIXED | 1 | 1 ms (the knob locks) | 50 ms (the knob locks) |
| FIX/MAN | 2 | 1 ms (fixed) | 50 ms after a peak shorter than DELAY; RELEASE after a longer one |

FIX/MAN (D §2.2 [V S13]) is described as a fast fixed attack, where "after a short peak it releases fast. The Attack
knob becomes a delay before the manual Release takes over, and this applies only when the peak is shorter than the
Attack setting". It is DualRelease (Bus G's AUTO law):
- the fixed 50 ms is the fast path;
- the ATTACK knob, relabelled DELAY, sets the slow path's charge;
- RELEASE is the slow path's release.

The charge is a one-pole over 2 × DELAY [H, fitted]. It also charges while the fast path falls, so at 1 × DELAY a 30 ms
peak under a 100 ms DELAY kept enough charge to hold the release for 284 ms. At 2 × it releases fast.

Measured by `dsp.optotube1b`:
- FIXED: attack 1.000 ms, release 61 ms (50 ms plus the cell's hold and afterglow).
- FIX/MAN with DELAY 100 ms and RELEASE 1 s: 61 ms after a 30 ms peak, 1.056 s after a 1 s passage.
- MANUAL with the same knobs: 1.23 s after the 30 ms peak.

**MANUAL is `tmode` 0.** It was first placed in the hardware's order, FIXED / MANUAL / FIX-MAN, with MANUAL at 1. A
Mode switch keeps the raw value, and Clean's and Brickwall's `tmode` 1 is AUTO. So a switch from this Mode into them
landed in their AUTO release, whose crest detectors are not part of the carry and took about 200 ms to settle.
`dsp.switch` caught it: a 6.9 dB click into Brickwall, and 1.01 dB of level deviation into Clean. With MANUAL at 0,
every Mode's manual timing shares a value.

## THRESHOLD, RATIO, KNEE

- **THRESHOLD** reads +20…−40 around 0 VU (−18 dBFS, ADR-59), plus OFF (D §2.2 [V S13]). It is a hybrid: the
  continuous dial, with OFF as the step outside it, where nothing compresses (`dsp.optotube1b` off row: 0 dB at 0 dBFS).
- **RATIO** is continuous from 2:1 to 10:1. 20 dB over the threshold the GR is S × 20 dB exactly, at both ends.
- **KNEE** is the opto cell's soft knee, locked at 10 dB [H] (D §5.3 "soft opto" [U]).

## The tube stage's level

TubeTransformer at Opto 2A's calibration, the voice locked on, made the audio-measured release of `dsp.time`'s
Quality rows differ by 2.2 samples between ECO and HQ, against a limit of 1.5. The cause is the stage's small
level-dependent gain run at each Quality's oversampled rate: a 0.5 s release turns a sub-millidecibel difference into
samples. With the drive 24 dB down the spread was 0.68 samples. The CL 1B's tube stage is clean at working levels, so
the Mode runs it kTubeTrimDb = −12 dB under Opto 2A's, which gives 1.06 samples.

## Heuristic constants [H]

| Constant | Value | Where | Source and reason |
|---|---|---|---|
| Dial zero | −18 dBFS | `OptoTube1BDesc.cpp` `kDialZeroDbfs` | ADR-59 |
| Knee | 10 dB | `kKneeDb` | the opto cell's soft knee |
| FIXED times | 1 ms / 50 ms | `kFixedAttackMs`, `kFixedReleaseMs` | D §2.2 [V S13] |
| FIX/MAN charge | 2 × DELAY | `kDelayChargeShare` | fitted: a peak shorter than DELAY releases fast |
| FIX/MAN spec | 0.05 s … 1.5 × RELEASE | `kProgramHiShare` | the cascade's 1/e runs past RELEASE |
| Tube trim | −12 dB | `kTubeTrimDb` | the Quality spread (above); the unit is clean at working levels |

## Verification

| Probe | What it holds Opto Tube 1B to |
|---|---|
| `dsp.optotube1b` | FIXED's 1 ms / 50 ms whatever the knobs; FIX/MAN's fast release after a short peak and the manual one after a long one, with the fixed attack; the continuous ratio's ends; THRESHOLD OFF |
| `dsp.time.opto-tube-1b` | MANUAL across the knobs' ranges; the Quality spread ≤ 1.5 samples |
| `dsp.switch.opto-tube-1b` | clean hand-overs with every lower Mode (MANUAL at `tmode` 0) |
| `ui.curve.opto-tube-1b` | the TRANSFER curve at every configuration (the span row now skips a curve that only grazes the frame) |
| `dsp.static`, `dsp.zipper`, `dsp.srsweep`, `dsp.analysis`, `dsp.hostile`, `dsp.link`, `dsp.null`, `dsp.print`, `dsp.rt`, `dsp.latency` | the generic per-Mode rows |

## Known limits

- The unit's topology is [U]. Feed-forward is chosen for its real threshold and ratio.
- GAIN's "off" position is not modelled.
- The Mk II's side-chain low-cut frequency range is [U]; it is 0–300 Hz here.

## Revision history

| Revision | Release | Change |
|---|---|---|
| 1 | v1.2 (unreleased) | First version. Not shipped yet (no `modes-ever.tsv` row until the release). |
