# Clean (slot 0, `clean`)

Mode sheet (01 §8.4 step 6): what Clean runs, every heuristic constant with its source and reason, and the revision
history. Owned by the Mode's card (F9 in S3). The contracts are 01 §10.3 (descriptor, traits) and 01 §5.2–5.3
(policies, engine); the code is `Source/fcdsp/modes/clean/` and the policy headers named below.

## Identity

| Field | Value |
|---|---|
| Key, slot | `clean`, 0 (`Modes.def`) |
| Group, rigor, family | modern, `Rigor::clean`, `CurveFamily::textbook` |
| Topology | feed-forward only (`kTopologies = FF`) |
| Revision | 1 |
| Provisional | no (cleared by F9, S3; its fidelity rows are blocking spec rows and its goldens can be blessed) |

## Traits (Clean.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `DetSelect<PeakLog, RmsLog, DualDet>` | DETECT PEAK / RMS / PK+RMS (`det`, a kernel key) |
| Computer | `QuadKnee` | THRESHOLD −60…0 dB, RATIO 1:1…∞:1 (S = 1 − 1/R), KNEE 0–72 dB, RANGE 0–60 dB |
| Link | `LinkMax` | LINK 0–100 % |
| Ballistics | `Hold<CrestAuto<SmoothBranching>>` | ATTACK 5 µs–250 ms, RELEASE 5 ms–5 s, HOLD 0–500 ms, TIME MODE MAN/AUTO |
| Stage 2 | `NoStage2` | – |
| Colour | `ColourSelect<ColourNone, TubeSym, DiodeAsym, Bright>` | VOICE OFF / TUBE / DIODE / BRIGHT (`voice`, a kernel key), DRIVE ±24 dB |
| SC shape | `Flat` | (the host's SC HPF and SC TILT act before it) |

The GR-domain smoothing sits after the gain computer (E §2.4 placement 3), so ATTACK and RELEASE act on GR changes
whatever the detector.

## Detectors

| DETECT | Policy | Level | Curve axis (`cleanLaw`) |
|---|---|---|---|
| PEAK | `PeakLog` | instantaneous 20·log10\|v\| | peak (a sine reads its peak) |
| RMS | `RmsLog` (`RmsLogT<20000>`) | 10·log10 of a mean-square one-pole, τ = 20 ms | rms (a sine reads peak − 3.01 dB) |
| PK+RMS | `DualDet` | (peak dB + RMS dB + 3.01) / 2 | peak (a sine reads its peak) |

## Voices

Every voice is a memoryless shaper f (f(0) = 0, f′(0) = 1) at input scale k = kIn · 10^(DRIVE/20), level-compensated
(y = x + (A(k·x) − k·x)/k, so small signals pass at unity), with A = f through Adaa.h's ADAA-1 residual scheme (the
linear part is never delayed). Because the voices are memoryless, the COLOUR view (`colourCurve`, f(k·x)/k) and the
describing-function static curve (E §6.3) are exact, and `dsp.static` checks each voice's fundamental gain against
them. The shape does not depend on GR (`colourStatic = true`). DRIVE is smoothed per control tick (20 ms, in dB).

| VOICE | Policy | f | kIn |
|---|---|---|---|
| OFF | `ColourNone` | identity: the wet path is untouched, bit for bit (D8 (b) null) | – |
| TUBE | `TubeSym` | tanh (odd harmonics, soft) | 1/2 |
| DIODE | `DiodeAsym` | (tanh(u + b) − tanh b) / (1 − tanh² b), b = 1/4 (even + odd), residual DC-blocked at 10 Hz | 1/2 |
| BRIGHT | `Bright` | u − 4u³/27 up to \|u\| = 3/2, then ±1 (clean, then a C¹ corner with 1/n³ upper odd harmonics) | 3/4 |

THD at −10 dBFS, 1 kHz, 48 kHz, no GR (the `static.voice.*.thd_db` golden rows): TUBE −53.8 dB (0 dB drive) /
−30.5 dB (+12 dB); DIODE −34.4 / −23.0 dB; BRIGHT −53.7 / −28.8 dB.

## TIME MODE AUTO (CrestAuto)

The release follows the side chain's crest factor (E §2.5a after Giannoulis, Massberg & Reiss, JAES 2013): the crest
detectors are a peak² detector (instant attack, 200 ms release) and a 200 ms mean square on the shaped SC, C² =
peak² / ms (≥ 1), and

    τR,eff = max(2·τR / C² − τA, τR / 8)

per sample, with τR and τA the published (smoothed) times. A sine (C² = 2) releases at τR − τA, a square or a sustained
tone (C² → 1) up to 2·τR − τA, drums (C² ≈ 10–30) much faster. The REL EFF readout and `EngineTelemetry::releaseNowMs`
report τR,eff; the CREST readout reports 10·log10 C². The status bit b2 (auto-slow) is set while τR,eff > τR.

## HOLD (Hold)

While the target GR is at or above the GR, the hold counter is re-armed to round(HOLD · fs / 1000) samples; when the
target falls below, the GR stays exactly still for that many samples, then releases. HOLD 0 leaves the ballistics
unchanged bit for bit.

## Heuristic constants [H]

| Constant | Value | Where | Source and reason |
|---|---|---|---|
| RMS window τ | 20 ms | `RmsLog = RmsLogT<20000>` | E §2.4/§2.5c (dbx uses ~30 ms); 20 ms keeps the 2·f0 ripple of a 1 kHz sine below 0.02 dB (D1 holds 0.05 dB at ∞:1: measured 0.017 dB) and follows a phrase |
| PK+RMS blend | mean of peak dB and RMS dB + 3.01 | `DualDet` | a sine reads its peak on both halves, so the curve axis stays the peak law; a transient reads half-way between its edge and its body |
| Crest detectors τ | 200 ms | `CrestAuto::kCrestTauMs` | E §2.5a ("both detectors over ~200 ms"), CTAGDRC |
| AUTO release floor | τR / 8 | `CrestAuto::kMinReleaseFraction` | keeps a long attack (2τR/C² − τA < 0) from driving the release to zero |
| TUBE kIn | 1/2 | `TubeSym::kIn` | 0 dBFS at 0 dB drive → shaper input 0.5 (≈ 2 % H3), −18 dBFS → 0.06 (≈ 0.03 %): audible only when pushed, as D §2.8's Pro-C 3 character |
| DIODE kIn, bias | 1/2, 1/4 | `DiodeAsym::kIn`, `kBias` | the same headroom as TUBE, H2 ≈ 0.12·u relative |
| DIODE DC blocker | 10 Hz | `DiodeAsym::kDcHz` | an asymmetric shaper makes DC ∝ u²; 10 Hz is inaudible and leaves 1 kHz at 1 − 5e-5 |
| BRIGHT kIn | 3/4 | `Bright::kIn` | reaches the soft clipper's corner (\|u\| = 3/2) at +6 dBFS with 0 dB drive |

The drive smoothing (20 ms) and the voice memorylessness follow from 01 §5.1 and the colourCurve contract (01 §7);
they are not tuning constants.

## Verification

| Probe | What it holds Clean to |
|---|---|
| `dsp.static.clean` | D1 at 1 kHz: every ratio × threshold × knee (PEAK), both other detectors, every voice at 0 and +12 dB drive (declared curve incl. the describing-function gain, judged at 192 kHz where ADAA-1's one-sample kernel is transparent), AUTO and HOLD; the textbook formula; tap against audio GR |
| `dsp.time.clean` | D2 attack/release at min/mid/max, GR OFF ramp, the carry hand-over, telemetry and internals |
| `dsp.quant.clean` | every stepped parameter (TIME MODE, DETECT, STEREO, VOICE, AUTO) through the resolver, D1 between detents |
| `dsp.srsweep.clean` | D1/D2 at 44.1–192 kHz against 48 kHz, 22.05/384 kHz robustness, HOLD and AUTO across rates, the 5 s release within 0.001 dB of its exact exponential at 48 and 384 kHz |
| `dsp.registry` | the descriptor lint; `fb.monotone` (no FB configuration in Clean) |

## Known limits

- At 48 kHz ECO, ADAA-1 low-passes a voice's distortion residual: the fundamental's saturation loss is about 0.75 %
  smaller than the memoryless curve predicts at 1 kHz (0.12 dB at 8 dB of saturation). STD/HQ run the colour at the
  oversampled rate, where this shrinks with the square of the factor.

## Revision history

| Revision | Sprint | Change |
|---|---|---|
| 1 | S3 (F9) | Full traits (01 §10.3), provisional cleared. No shipped print hash yet, so no bump. |
