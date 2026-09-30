# Console E (slot 9, `console-e`)

Mode sheet (01 §8.4 step 6): what Console E runs, every heuristic constant with its source and reason, and the revision
history. Console E is the second Wave 2 Mode (ADR-77, ADR-78), added by the lead without a sprint card. The contracts
are 01 §10.4 (descriptor, traits) and 01 §5.2–5.3 (policies, engine); the code is `Source/fcdsp/modes/console-e/` and
the policy headers named below. The unit is D §2.4's SSL E/G channel dynamics (M10): the compressor section of a
console channel strip.

## Identity

| Field | Value |
|---|---|
| Key, slot | `console-e`, 9 (`Modes.def`, Wave 2) |
| Group, rigor, family | vca, `Rigor::character`, `CurveFamily::textbook` |
| Topology | feed-forward only (`kTopologies = FF`) |
| Revision | 1 |
| Provisional | no (its own traits are installed; the fidelity rows block and the goldens are blessed) |

## Traits (ConsoleE.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `DetSelect<RmsLogT<10 ms>, PeakLog>` | DETECT RMS (default) / PEAK |
| Computer | `QuadKnee` | THRESHOLD dial +10 … −20 (−8 … −38 dBFS); RATIO 1:1 … ∞ continuous; KNEE OVEREASY (10 dB) / HARD; RANGE 0–60 dB (extension) |
| Link | `LinkMax` | LINK DUAL / LINK (the larger GR drives both) |
| Ballistics | `VcaChannelT<0, 1>` | ATTACK AUTO (3–30 ms, program) / FAST (1 ms); RELEASE 0.1–4 s; REL CURVE LOG / LIN |
| Stage 2 | `NoStage2` | – |
| Colour | `ColourNone` | VOICE and DRIVE n/a: the channel VCA is clean |
| SC shape | `Flat` | SC HPF 0–350 Hz (the host's SC HPF; the console's channel HPF into the side chain) |

`m[]` slots (ConsoleE.h): 0 AUTO attack on, 1 LIN release on.

## THRESHOLD and AUTO makeup

The dial reads +10 … −20 dB around the operating level (D §2.4 [V S26]), 0 VU = −18 dBFS (ADR-59), as Bus G's dial
does: dial 0 is −18 dBFS, the host default.

The unit's makeup is automatic, "calculated from the Ratio and Threshold settings" (D §2.4 [V S26]), so AUTO is locked
on and MAKEUP is an extension trim after it. The makeup is the static curve's GR at 0 VU (−18 dBFS at the detector,
`kMakeupRefDbfs` [H]). So a signal at the operating level keeps its level as THRESHOLD falls and RATIO rises. That is
SSL's description: the output "remains constant". `dsp.consolee` measures a 0 VU square at −18.0000 dBFS out at both
T −18 and T −28 dBFS.

The engine's own AUTO law is different. It is r^(0 dBFS) (E §2.2), used by Clean and Bus 25, and it would lift a 0 VU
signal by up to 18 (1 − 1/R) dB: +12.6 dB at this Mode's defaults. So `consoleEPhysical` clears `kEngAutoMakeup`, as
Brickwall does, computes QuadKnee's static GR at the reference itself, and adds it to `makeupDb`. The host smooths that
like any makeup, and the telemetry's effective makeup shows it.

## Ballistics (VcaChannel.h)

The ballistics are built on SmoothBranching's GR-domain step, keeping its time smoothing, branch rule and sub-ulp
carry. Per lane:
- **ATTACK AUTO:** the rate is min(c(3 ms), c(30 ms) · (1 + (t − r) / D0)) with D0 = 2 dB. It grows with the
  overshoot, so a transient is caught in about 3 ms and a slowly rising level at 30 ms: "adjustable between 3ms and
  30ms", program-dependent (D §2.4 [V S27]).
  - Measured with a peak detector at ∞:1: 5.51 ms to 1/e for a 15 dB overshoot, 15.7 ms for 3 dB.
  - Measured with the default RMS detector (`dsp.time`): 11.3 ms.
- **ATTACK FAST:** SmoothBranching at 1 ms (the F.ATK button); 1.000 ms measured.
- **RELEASE LOG:** SmoothBranching's exponential in dB, with RELEASE as τ. This is the Revision 4 logarithmic release.
- **RELEASE LIN:** a constant L = 10 dB per RELEASE ([H]; the E module's linear-release switch). The step is exactly
  −s = −L / (τ_R fs) dB per sample, run as a rate on the remaining distance, so the carry keeps it exact.
  - Measured at 1 s: 10.0000 dB/s, with equal 16 → 10 and 10 → 4 dB legs. LOG at 1 s gives legs in the ratio
    ln 2.5 / ln 1.6.
  - The release spec is a slew rate (`TimeLaw::rateDbPerS`).

Switching AUTO or LIN blends the rates over 20 ms. The GR never steps, and neither does its slope (Bus G's AUTO
lesson). A LOG → LIN switch in the middle of a release changes the GR by at most 3.4e-4 dB per sample, which is the
release's own slope. With both switches off the policy is SmoothBranching bit for bit.

## Detector

The E module's side chain is "true RMS", while the strip guide says "peak sensing" [C]; the 9000 J/K has a switch
(D §2.4 [V S27]). DETECT therefore offers RMS (default) and PEAK. The RMS window is 10 ms ([H]): shorter than Clean's
20 ms so a channel follows a phrase closely. Its ripple on a 1 kHz sine is about ±0.035 dB.

## Heuristic constants [H]

| Constant | Value | Where | Source and reason |
|---|---|---|---|
| Dial zero | −18 dBFS | `ConsoleEDesc.cpp` `kDialZeroDbfs` | the dial reads around the operating level; ADR-59 |
| OVEREASY width | 10 dB | `kOverEasyDb` | a soft knee (D §2.4); 0.9375 dB of GR at the threshold at 4:1 |
| Makeup reference | −18 dBFS (0 VU) | `kMakeupRefDbfs` | the output "remains constant" for a nominal-level signal |
| AUTO range | 3–30 ms | `VcaChannelT::kAutoFastMs`, `kAutoSlowMs` | published (D §2.4 [V S27]) |
| AUTO D0 | 2 dB | `VcaChannelT::kAutoKneeDb` | a 15 dB transient at about 5 ms, a 3 dB rise at about 16 ms: across the published range |
| LIN rate | 10 dB per RELEASE | `VcaChannelT::kLinDb` | the knob's 0.1–4 s spans 100–2.5 dB/s |
| RMS window | 10 ms | `ConsoleE::kRmsTauUs` | follows a phrase; small ripple at 1 kHz |

## Verification

| Probe | What it holds Console E to |
|---|---|
| `dsp.consolee` | AUTO's program dependence and range, FAST 1 ms, LIN's constant rate and its spec, LOG's exponential, a click-free LOG → LIN switch, both knees, the auto makeup (the curve at 0 VU, constant output) |
| `dsp.time.console-e` | ATTACK AUTO in its program range; RELEASE across 0.1–4 s; GR OFF's ramp |
| `dsp.static`, `dsp.zipper`, `dsp.switch`, `dsp.srsweep`, `dsp.analysis`, `dsp.hostile`, `dsp.link`, `dsp.null`, `dsp.print`, `dsp.rt`, `dsp.latency` | the generic per-Mode rows |

Console E exposed three things in the shared probes and stages:
- **`dsp.srsweep`:** its long-release row assumed a target that steps. An RMS detector lowers the target over its
  window, which read 0.05 dB. The row now follows the tapped target in double; for a peak detector that is the same
  exponential.
- **`dsp.time`'s GR OFF rows:** the Rig applies makeup unsmoothed, so an AUTO makeup that follows GR OFF read as a step.
  The host ramps that change. The rows now clear `kEngAutoMakeup`.
- **`VoiceDrive` (TubeSym.h):** it stepped a moving DRIVE once per colour call. Against this Mode's clean output that
  read 4.5 dB on `dsp.zipper`'s DRIVE edge (limit 3). Other Modes masked it with their own colour. Fixed for every
  Mode by ADR-86 (DRIVE glides per sample; with a temporary live DRIVE this Mode then read under 0.8 dB). Console E
  still has no DRIVE: the channel has no drive control.

## Known limits

Not modelled:
- the expander/gate section;
- the E module's routing of the channel EQ into the side chain;
- the audible E/G revision difference ([U]).

The OVEREASY width, the AUTO law's D0, the LIN rate and the makeup reference are [H]. A measured unit would refit them.

## Revision history

| Revision | Release | Change |
|---|---|---|
| 1 | v1.2 (unreleased) | First version. Not shipped yet (no `modes-ever.tsv` row until the release). |
