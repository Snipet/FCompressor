# Bus G (slot 1, `bus-g`)

Mode sheet (01 §8.4 step 6): what Bus G runs, every heuristic constant with its source and reason, and the revision
history. Owned by the Mode's card (M1 in S7). The contracts are 01 §10.4 (descriptor, traits) and 01 §5.2–5.3
(policies, engine); the code is `Source/fcdsp/modes/bus-g/` and the policy headers named below. The unit is D §2.4's
SSL G-series bus compressor.

## Identity

| Field | Value |
|---|---|
| Key, slot | `bus-g`, 1 (`Modes.def`) |
| Group, rigor, family | vca, `Rigor::modelled`, `CurveFamily::textbook` |
| Topology | feed-forward only (`kTopologies = FF`) |
| Revision | 1 |
| Provisional | no (cleared by M1, S7: its fidelity rows are blocking spec rows and its goldens can be blessed) |

## Traits (BusG.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `PeakLog` | DETECT locked PEAK ([U] D §8.3) |
| Computer | `QuadKnee` | THRESHOLD dial −15…+15 (−33…−3 dBFS), RATIO 2 / 4 / 10 (S = 0.5 / 0.75 / 0.9), KNEE derived from RATIO, RANGE 0–60 dB (extension) |
| Link | `LinkMax` | LINK 0–100 % (extension; the hardware sums to one detector) |
| Ballistics | `AutoSwitch<SmoothBranching, DualRelease>` | ATTACK .1 / .3 / 1 / 3 / 10 / 30 ms; RELEASE .1 / .3 / .6 / 1.2 s on `SmoothBranching`, AUTO (the fifth position) on `DualRelease` |
| Stage 2 | `NoStage2` | – |
| Colour | `ColourSelect<VcaBus, ColourNone>` | VOICE VCA (+ DRIVE ±12 dB, extension) / CLEAN (`voice`, a kernel key) |
| SC shape | `Flat` | (the host's SC HPF, an extension, acts before it) |

The switch values are the published ones (D §2.4 [V~ S5]): each is a `Step` whose plain value is the published time
(τ, `TimeLaw::expDb`) or ratio. `dsp.time.bus-g` measures every ATTACK and RELEASE detent (`time.atk.d0…d5`,
`time.rel.d0…d4`), `dsp.static.bus-g` every RATIO detent at two thresholds (`static.r2/r4/r10.*`), `dsp.quant.bus-g`
their snap boundaries (2/4/10 at S = 0.625 / 0.825), all blocking.

## THRESHOLD: the dial offset

The hardware dial reads −15…+15 dB around the console's operating level (D §2.4 [V~ S5]; some console versions ±20,
[C/U], not modelled). ADR-59 fixes that level: 0 VU = +4 dBu = −18 dBFS. So **dial 0 = −18 dBFS** and the dial spans
−33…−3 dBFS (`DisplayMap` dial = plain + 18). The default is the dial's centre, which is also the universal host
default and Clean's default (−18 dBFS): a fresh Bus G, and a switch from Clean's default, read dial 0. 01 §10.4 had
dial 0 = −15 dBFS [H]; the fit moves it by 3 dB (the Mode has not shipped: no revision bump).

## RELEASE AUTO (DualRelease)

E §2.5b, after the SSL G circuit description ("the smaller capacitor charges first … if the signal stays at high level
long enough, then the larger capacitor will also charge"), in the GR domain, per lane:

    r_f ← smooth-branching(target)      attack τ_A (the ATTACK switch), release τ_Rf
    r_s ← one-pole toward r_f           τ_C while r_f > r_s (charging), τ_Rs otherwise
    r    = max(r_f, r_s)

A short transient charges r_f only, so the GR comes back at τ_Rf; sustained program charges r_s too, which then holds
the GR and lets it go at τ_Rs ("dependent upon the duration of the program peak", D §2.4). Both paths are
`SmoothBranching` one-poles (log-domain time smoothing; the slow path carries its rounding remainder, so a long release
follows its exponential to the target). The FAST ENV, SLOW ENV and AUTO SLOW internals are r_f, r_s and
[r_s > r_f]; the status bit b2 (auto-slow) is set while the slow path holds the GR, and the release telemetry
reports τ_Rs then, τ_Rf otherwise.

The switch into and out of AUTO (`AutoSwitch`) seeds the path that was idle from the running one's GR and crossfades
the two over 20 ms with the host's smootherstep; the path that becomes selected lands on its own times at once, the one
fading out keeps its coefficients. A hard hand-over was continuous in GR but not in slope and read +4.4 dB on
`dsp.zipper`'s AUTO → 1.2 s detent-edge click row (limit +3); the blend reads −0.4 dB (ECO) / +0.9 dB (STD).

Measured through the engine (`dsp.dualrelease`, 4:1, ATTACK .1 ms, 15 dB of GR, release to 1/e in the switch's law):

| Burst before the release | 10 ms | 100 ms | 300 ms | 1 s | 5 s | dsp.time's 0.5 s |
|---|---|---|---|---|---|---|
| Release | 0.100 s | 0.179 s | 0.725 s | 1.073 s | 1.105 s | 0.917 s |

Every value lies in the published span of the switch, 0.1–1.2 s, which is the AUTO step's program range
(`kit::releaseFromView`, judged by `time.rel.d4.s`).

## Heuristic constants [H]

| Constant | Value | Where | Source and reason |
|---|---|---|---|
| Dial zero | −18 dBFS | `BusGDesc.cpp` `kDialZeroDbfs` | the dial reads around the operating level (D §2.4); ADR-59: 0 VU = −18 dBFS |
| Knee per ratio | 10 / 6 / 3 dB at 2 / 4 / 10 | `kKneeByRatio` | E §2.2: the knee width follows the ratio ("moderately soft", D §2.4); its dependence on the threshold is unpublished (D §8.3) and not modelled |
| τ_Rf (AUTO fast release) | 100 ms | `kAutoFastReleaseMs` → `m[0]` | E §2.5b: "a fast one near the fastest release setting (0.1 s)"; a transient's AUTO release equals the 0.1 s position |
| τ_C (AUTO charge) | 300 ms | `kAutoChargeMs` → `m[1]` | how long a program peak must last before the release turns slow (the slow path reaches 63 % of the GR); no published figure |
| τ_Rs (AUTO slow release) | 1.0 s | `kAutoSlowReleaseMs` → `m[2]` | E §2.5b: "a slow one near the slowest (1.2 s)", fitted so a charged AUTO release, the two-pole cascade r_f → r_s, measures 1.105 s to 1/e: inside the switch's 1.2 s end (τ_Rs = 1.2 s would measure 1.305 s: the lag of r_f adds about τ_Rf) |
| VcaBus shaper | C¹ cubic soft clip, f(u) = u − 4u³/27, rails at \|u\| = 3/2 | `VcaBus.h` | the solid-state bus amplifier: odd (symmetry-trimmed VCAs, push-pull bus amplifiers: H2 nulls), clean until the rail corner |
| VcaBus kIn | 1/10 | `VcaBus::kIn` | H3 of a 0 dBFS sine at −68.6 dB (0.04 %; −104.6 dB at 0 VU) at 0 dB drive, −44.5 dB at +12 dB (the rail corner then at +11.5 dBFS): D §2.4's "low" colour, and inside a modelled Mode's meter-truth budget: a +6 dBFS sine's fundamental moves 0.039 dB, a +2 dBFS square's y/x 0.020 dB (tap-vs-audio ≤ 0.05 dB), and 20 dB below the default knee (−41 dBFS) the colour residual is −140 dB (the modelled below-threshold null asks −120 dB) |

The detector law (peak), the absence of a side-chain filter on the original (the SC HPF is an extension at OFF) and the
linked stereo detector (LINK 100 % default) are D §2.4's [U] items; the drive smoothing (20 ms) and the voice's
memorylessness follow from 01 §5.1 and the colourCurve contract (01 §7).

## Voices

| VOICE | Policy | f | kIn |
|---|---|---|---|
| VCA (VCA + CONSOLE) | `VcaBus` | u − 4u³/27 up to \|u\| = 3/2, then ±1 (odd; the rails) | 1/10 |
| CLEAN | `ColourNone` | identity: the wet path is untouched, bit for bit; DRIVE is n/a | – |

The shaper is memoryless and level-compensated (y = x + (A(k·x) − k·x)/k, A through Adaa.h's ADAA-1 residual scheme), so
the COLOUR view and the describing-function static curve are exact (`colourStatic = true`); `dsp.static`'s voice rows
hold CLEAN to its curve and prove the colour never feeds the control path (`gr_matches_off`); `dsp.analysis` checks
VCA's harmonic analysis against its own DFT and prints it against the engine's (H3 −80.67 dB engine, −80.67 dB static
at −6 dBFS: under that row's −70 dB judging floor, so `dsp.dualrelease` holds the design figures instead).

## Verification

| Probe | What it holds Bus G to |
|---|---|
| `dsp.static.bus-g` | D1 at 1 kHz: every ratio detent × threshold {−30, −10} dBFS (knee derived), at the fastest attack and the slowest release position (AUTO), VOICE VCA; the textbook formula; tap against audio GR; CLEAN at 0 and +12 dB drive |
| `dsp.time.bus-g` | D2 at every ATTACK and RELEASE detent against the published τ; AUTO in its program range; GR OFF ramp; the carry hand-over; telemetry and internals |
| `dsp.quant.bus-g` | every stepped parameter's detents and snap boundaries through the resolver, D1 between detents |
| `dsp.zipper.bus-g` | every detent edge click-free, AUTO ↔ 1.2 s included (K2 #4 iv), at ECO and STD |
| `dsp.dualrelease` | the policies: DualRelease FF against its recurrence in double, the FB affine maps and max of roots against bisection (for Diode 609), the commit rules, AutoSwitch's bit-exactness and hand-over, Bus G's AUTO program dependence and internals, VcaBus's design figures |
| `dsp.srsweep`, `dsp.link`, `dsp.analysis`, `dsp.hostile`, `dsp.null`, `dsp.switch`, `dsp.rt`, `dsp.print`, `dsp.latency` | the generic per-Mode rows, fidelity rows blocking |

## Known limits

- The knee does not depend on the threshold (E §2.2's W = f(T, R) is unpublished); the Solid State Bus-Comp dataset
  (E §2.8) is the corpus for a later fit (`Tools/ModeFit`).
- In a feedback kernel (not Bus G's: DualRelease's FB forms are for Diode 609), while the slow root holds the GR the
  fast path advances as its own loop's root, an upper bound of its exact coupled value (DualRelease.h); both settle on
  the same static FB equilibrium.

## Revision history

| Revision | Sprint | Change |
|---|---|---|
| 1 | S7 (M1) | Full traits (01 §10.4), provisional cleared; [H] constants fitted (dial zero −18 dBFS, τ_Rs 1.0 s). Not shipped (no `modes-ever.tsv` row), so no bump. |
