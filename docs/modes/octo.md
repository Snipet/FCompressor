# Octo (slot 8, `octo`)

Mode sheet (01 §8.4 step 6): what Octo runs, every heuristic constant with its source and reason, and the revision
history. Octo is the first Wave 2 Mode (post-v1, SPRINTS §5; ADR-77), added by the lead without a sprint card. The
contracts are 01 §10.4 (descriptor, traits) and 01 §5.2–5.3 (policies, engine); the code is `Source/fcdsp/modes/octo/`
and the policy headers named below. The unit is D §2.7's hybrid VCA (M18): a feed-forward VCA compressor with a
ratio switch that reaches an opto-style release and a brick wall, a detector band emphasis, and a distortion
generator after the gain element.

## Identity

| Field | Value |
|---|---|
| Key, slot | `octo`, 8 (`Modes.def`, Wave 2) |
| Group, rigor, family | vca, `Rigor::character`, `CurveFamily::custom` |
| Topology | feed-forward only (`kTopologies = FF`) |
| Revision | 1 |
| Provisional | no (its own traits are installed; the fidelity rows block and the goldens are blessed) |

## Traits (Octo.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `PeakLog` | DETECT locked VCA (the unit's peak detector) |
| Computer | `QuadKnee` | INPUT dial 0–10 into a fixed −18 dBFS threshold; RATIO 1 / 2 / 3 / 4 / 6 / 10 (OPTO) / 20 / NUKE; KNEE derived from RATIO; RANGE 0–60 dB (extension) |
| Link | `LinkMean` | LINK DUAL / LINK (the unit's Link sums both inputs) |
| Ballistics | `AutoSwitch<SmoothBranching, DualReleaseT<0,1,2>, OptoSelect>` | ATTACK .05–30 ms; RELEASE .05–3.5 s on `SmoothBranching`; at 10:1 OPTO, `DualRelease` and RELEASE up to 20 s |
| Stage 2 | `NoStage2` | – |
| Colour | `ColourSelect<OctoDistT<0,false>, …, OctoDistT<2,true>>` | AUDIO CLEAN / HP / DIST 2 / DIST 2 + HP / DIST 3 / DIST 3 + HP; DRIVE ±24 dB (extension) |
| SC shape | `BandEmphasisT<3>` | BAND EMPH OFF / BE (+10 dB at 6 kHz); SC HP OFF / 90 Hz (the host SC HPF) |

`m[]` slots (Octo.h): 0 fast release, 1 charge, 2 slow release (DualRelease's), 3 emphasis gain (dB), 4 the OPTO flag
(`OptoSelect::useB`).

## INPUT and OUTPUT: fixed threshold, gain into it

The unit's INPUT "drives into a fixed threshold" (D §2.7 [V S14]). `octoPhysical` sets `thrDb = −18 dBFS` (0 VU,
ADR-59) and `preGainDb = −18 − thr`: the INPUT knob is a gain in front of both the detector and the distortion
generator, so pushing it compresses and distorts more at once, which is the unit's character. The host's threshold
parameter stays the stored value (−38 … +2 dBFS, default −18): INPUT's dial shows `5 + (−18 − thr) / 4`, 4 dB per unit,
so dial 5 is the host default and Clean's, and 0 … 10 spans ±20 dB. OUTPUT is the makeup at 4 dB per unit with unity at
5 (the unit's "Output at 8" is its tape level, a calibration FCompressor does not reproduce).

## RATIO and the knee

| RATIO | 1 | 2 | 3 | 4 | 6 | 10 (OPTO) | 20 | NUKE |
|---|---|---|---|---|---|---|---|---|
| S | 0 | 0.5 | 0.66667 | 0.75 | 0.83333 | 0.9 | 0.95 | 1.0 |
| Knee (dB) | 6 | 18 | 14 | 10 | 8 | 5 | 2 | 0 |

The knee follows the ratio (D §5.3 "P per ratio"): 2:1 and 3:1 are very wide ("parabolic knees"), 4:1 and 6:1
steeper, 10:1 short, 20:1 and NUKE hard. 1:1 is S = 0: no gain reduction, only the audio circuit.

## 10:1 OPTO (DualRelease)

At the OPTO step `OptoSelect` hands the ballistics to `DualReleaseT<0,1,2>` (Bus G's AUTO law, bus-g.md): a fast path
releasing at τ_Rf = max(50 ms, 0.2 × RELEASE), a slow path charging in τ_C = 0.8 s and releasing at τ_Rs = RELEASE,
GR = max of the two. A transient comes back at τ_Rf; sustained program charges the slow path, which holds the GR and
lets it go at RELEASE: an opto cell's memory. `AutoSwitch` crossfades the hand-over over 20 ms. RELEASE's range widens
to 20 s at this step (a `Variant` on `ratio`). The release telemetry and `octoReleaseSpec` report the program range
[τ_Rf, 1.5 × RELEASE] (the cascade's 1/e time runs past τ_Rs by about τ_Rf). The internals are FAST ENV, SLOW ENV and
OPTO SLOW, like Bus G's.

Measured (`dsp.octo`, RELEASE 2 s, 1 kHz 12 dB over the threshold, to 1/e): 2.405 s after 3 s of program, 0.400 s after
a 30 ms burst (share 0.17, limit 0.5); at 4:1 the same pair is 2.0 / 2.0 s (not program-dependent).

## BAND EMPH (BandEmphasisT)

An RBJ peaking bell in the side chain, 6 kHz, Q 1, +10 dB, as a TDF-II biquad per lane after the host filters and
before the detector (`stage::BandEmphasisT`, `Source/fcdsp/engine/stages/scshape/BandEmphasis.h`). The detector hears
more of the band (sibilance, cymbals); the audio is untouched. The gain is smoothed over 20 ms in dB, and 0 dB is an
exact bypass with the state cleared. `magDb` is the digital filter's own |H| (the SC view draws it). Measured: +10.000 dB
at 6 kHz, 0 at 100 Hz; a steady 6 kHz sine 5 dB under the threshold reads 3.7 dB of GR with the button and 0 without.

## AUDIO (OctoDistT)

A driven static voice after the gain element (`detail::processDriven`: level-compensated, only the residual through
ADAA-1), `y = x + (A(k x) − k x) / k`, `k = kIn × 10^(DRIVE / 20)`, then, on the HP positions, the audio high-pass.

| Kind | Shaper | kIn | At 0 VU (−18 dBFS) |
|---|---|---|---|
| CLEAN | `adaa::SoftClip` (odd) | 0.03 | THD −89.5 dB at 0 dBFS: meter truth holds at any INPUT |
| DIST 2 | `adaa::AsymTanh`, b = 0.35, residual through a 10 Hz DC blocker | 2.5 | H2 −25.9 dB, H3 −45.3 dB |
| DIST 3 | `adaa::Tanh` (odd) | 3.0 | H3 −38.8 dB ("more similar to tape") |

HP is a third-order Butterworth high-pass, −3 dB at 65 Hz, 18 dB/oct (D §2.7 [V S14]): a TPT one-pole and a TPT SVF
(Q = 1) at the same corner, at the OS rate. Measured: −3.010 dB at 65 Hz, −18.129 dB at 32.5 Hz, 0 at 1 kHz.

The voices are static (`colourStatic = true`): the COLOUR view and `dsp.static`'s describing-function curve are exactly
`f(k x) / k`. An earlier GR-weighted residual ("distorts more while compressing") broke that model; INPUT driving the
generator gives the same behaviour statically. Because every AUDIO position runs a shaper, DRIVE is live (not n/a) and
`hostile.silence` allows the voice's residual.

## Heuristic constants [H]

| Constant | Value | Where | Source and reason |
|---|---|---|---|
| Fixed threshold | −18 dBFS | `OctoDesc.cpp` `kT0` | "drives into a fixed threshold" (D §2.7); 0 VU per ADR-59 |
| Dial law | 4 dB per unit, unity at 5 | `kDialDbPerUnit` | a ±20 dB INPUT / OUTPUT span; the published dials are 0–10 with no dB scale |
| Knee per ratio | 6 / 18 / 14 / 10 / 8 / 5 / 2 / 0 dB | `kKneeByRatio` | D §5.3's per-ratio character (wide 2 and 3, hard 20 and NUKE) |
| OPTO fast share | 0.2 × RELEASE, ≥ 50 ms | `kOptoFastShare`, `kOptoFastMinMs` | a transient's release clearly faster than a sustained note's (transient share 0.17) |
| OPTO charge | 0.8 s | `kOptoChargeMs` | how long program must last to engage the slow tail; 0.3 s (Bus G's) made a 30 ms burst read 0.55 of the sustained time |
| OPTO spec hi | 1.5 × RELEASE | `kOptoReleaseHiShare` | the fast → slow cascade's 1/e time runs past τ_Rs |
| BAND EMPH | +10 dB, 6 kHz, Q 1 | `kEmphasisDb`, `BandEmphasisT` | "emphasized 6kHz band" (D §2.7 [V S14]); gain and width unpublished |
| AUDIO kIn | 0.03 / 2.5 / 3.0 | `OctoDistT::kInByKind` | CLEAN inside meter truth; DIST 2 and 3 about 1–5 % at 0 VU, tens of per cent at 0 dBFS ("Redline") |
| DIST 2 bias | 0.35 | `OctoDistT::kBias` | H2 dominant over H3 by ~20 dB |

## Verification

| Probe | What it holds Octo to |
|---|---|
| `dsp.octo` | OPTO program dependence and the 4:1 control; BAND EMPH response and running GR; AUDIO HP corners; DIST 2 / DIST 3 harmonic balance, CLEAN THD |
| `dsp.static.octo` | every RATIO step through `QuadKnee` with the derived knee; the voices' describing-function curves; tap against audio GR |
| `dsp.time.octo`, `dsp.zipper.octo`, `dsp.switch.octo` | ATTACK and RELEASE (OPTO in its program range), detent edges click-free, switching against every lower slot |
| `dsp.analysis`, `dsp.hostile`, `dsp.link`, `dsp.null`, `dsp.print`, `dsp.srsweep`, `dsp.rt`, `dsp.latency` | the generic per-Mode rows |

## Known limits

- Not modelled: the later model's British mode (an all-buttons emulation), NUKE's logarithmic release and 20:1's
  different release slope, and the special detector circuitry of 2:1, 10:1 and NUKE.
- The emphasis gain and width and the AUDIO drive levels are unpublished ([H]); a measured unit would refit them.

## Revision history

| Revision | Release | Change |
|---|---|---|
| 1 | v1.2 (unreleased) | First version. Not shipped yet (no `modes-ever.tsv` row until the release). |
