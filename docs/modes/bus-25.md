# Bus 25 (slot 6, `bus-25`)

Mode sheet (01 §8.4 step 6): what Bus 25 runs, every heuristic constant with its source and reason, and the revision
history. Owned by the Mode's card (M6 in S11). The contracts are 01 §10.7 (the sketch), 01 §5.2–5.5 (policies,
engine, the feedback step, kernel switching), ADR-63 and ADR-67; the code is `Source/fcdsp/modes/bus-25/` (the traits,
the descriptor and the two Mode-local policies `Bus25Rms.h` and `Bus25Ballistics.h`). The unit is D §2.4's API
2500: a VCA bus compressor whose TYPE switch runs one side chain feed-forward (NEW) or feedback (OLD).

## Identity

| Field | Value |
|---|---|
| Key, slot | `bus-25`, 6 (`Modes.def`) |
| Group, rigor, family | vca, `Rigor::modelled`, `CurveFamily::textbook` |
| Topology | VOICE (TYPE) NEW = feed-forward, OLD = feedback (`kTopologies = FF | FB`), a kernel key |
| Detector law | `rms` (a sine reads its peak − 3.01 dB) |
| Link law | `LinkLaw::cvSum` |
| Revision | 1 |
| Provisional | no (cleared by M6, S11: its fidelity rows are blocking spec rows and its goldens can be blessed) |

## Traits (Bus25.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `bus25::RmsCatch` (Mode-local) | DETECT locked RMS (THAT 2252): a mean square in a window of τR / 50 with a 20 dB jump catch (below) |
| Computer | `QuadKnee` | THRESHOLD −20…+20 dBu (−42…−2 dBFS, display dBu), RATIO 1.5 / 2 / 3 / 4 / 6 / 10 / ∞, KNEE HARD / MED / SOFT (W = 0 / 6 / 12 dB), RANGE 0–60 dB (extension, NEW only) |
| Link | `LinkCvSum` | L/R LINK IND / 50 / 60 / 70 / 80 / 90 / 100 %: the normalised CV node (F5, ADR-67); in OLD the node is solved inside the loop (below) |
| Ballistics | `bus25::CvSumBranching` (Mode-local) | ATTACK .03 / .1 / .3 / 1 / 3 / 10 / 30 ms; RELEASE .05 / .1 / .2 / .5 / 1 / 2 s, or VAR 50–3000 ms (TIME MODE) |
| Stage 2 | `NoStage2` | – |
| Colour | `ColourNone` | (the 2510/2520 and output transformer are not modelled: Known limits) |
| SC shape | `Flat` | THRUST is the host's side-chain tilt (`sce`), the SC HPF the standard extension |

`EngineParams::m` stays at its neutral zeros (S11 lead revision 4: THRUST is the host tilt alone, no second emphasis).

## TYPE: NEW (feed-forward) and OLD (feedback)

`physical()` sets `e.topo` from VOICE (NEW 0 → `kTopoFF`, OLD 1 → `kTopoFB`), so TYPE is a kernel key (01 §5.5): a flip
constructs the other kernel of the same Mode into the idle arena, seeds it from the running one's carry (applied GR,
detector level) and crossfades the two paths over 20 ms; `ModeEngine` branches per chunk on `e.topo`. OLD senses the
output in the log domain (x − r, E §2.6) with QuadKnee's closed-form FB root; its static curve is the FB curve (the knee
at the output, so it spans (1 + R) W / 2 at the input: "FB is smoother, softer", D §2.4 [V S6]); above the knee both
TYPEs reach the same ratio (R_eff = 1 + k = R).

`dsp.bus25topo` flips TYPE at a waveform peak of a 110 Hz tone with ~6 dB of GR (defaults, 1 ms attack) and of a
stereo pair at LINK 70 %, at ECO, STD and HQ: the click metric (energy above 8 kHz within ±2 ms against both steady
renders) reads −7.6 … +0.6 dB (limit +3; a hard splice reads +64 … +96 dB); before the edge the render is the steady
one bit for bit, and 1.8 s after it the GR is the steady target TYPE's within 0.05 dB.

## The RMS detector (RmsCatch) [H]

    ms ← max( ms + c (v² − ms),  v² − 100 ms )        level = 10 log10 ms        c = 1 − α of τw = τR / 50

- **The window follows the release.** A log-domain RMS detector's window is also its fall rate (4.343 / τw dB/s,
  E §2.5c); the 2252 averages on its timing capacitor. τw = τR / 50 [H]: 1 ms at the 50 ms position, 10 ms at the
  0.5 s default, 40 ms at 2 s, 60 ms at VAR's 3 s. A drop reaches the gain computer well inside every release position,
  so the RELEASE switch (SmoothBranching, in the GR domain) is what a release measures; the window's lag adds
  2.3 τw = +4.7 % on dsp.time's 20 dB step (inside the modelled 10 %; less for smaller drops). A steady 1 kHz sine
  reads its RMS within 0.035 dB at the default (0.009 dB at 2 s); 110 Hz within 0.3 dB (the window's 2 f0 ripple).
- **The jump catch.** The mean square never lags more than 20 dB (×100) below the instantaneous power: a louder sample
  lifts it to v² − 100 ms (a 40 dB jump lands 0.04 dB short in one sample; the window closes the rest). Program whose
  power stays within 20 dB of its mean square (steady tones, noise, mixes: 10 s of Gaussian noise never triggers it)
  reads a true RMS; an onset from 20 dB or more below reaches the gain computer at once, so the ATTACK switch alone
  times it. This is the strong end of an RMS detector's own program dependence (the larger the jump, the faster it
  charges, E §2.5c). The update is continuous and non-decreasing in v², and the detector keeps ONE state, so a carry
  (`Carry::detDb`) hands it over as RmsLog's does (`time.carry.seamless`).
- **Why not RmsLog.** DW's trial (a fixed 20 ms window in front of the ballistics) measured 4.1 ms for every attack at
  or below 0.3 ms, 5.3 ms at 1 ms, and 0.104 s on the 50 ms release (dsp.time, fidelity blocking); a window short
  enough for the fast detents (≤ 0.15 ms) ripples +1.7 dB on a 1 kHz sine, which dsp.static's fastest attack holds
  (computed: the one-pole's 2 kHz gain 0.47). No fixed linear window passes both; the release-tied window with the
  catch passes both with margin. The GR-OFF and bypass edges DW saw click with RmsLog are clean now
  (`time.groff.{off,on}` +0.14 / −5.7 dB, `null.eco.bypass.{on,off}` +0.15 / −6.1 dB, limit +3; the S7 smootherstep
  ramps).

Measured on dsp.time's D2 steps (1 kHz squares, T − 20 → T + 20 → T − 20 dB, law expDb, NEW):

| ATTACK | .03 | .1 | .3 | 1 | 3 | 10 | 30 ms |
|---|---|---|---|---|---|---|---|
| measured | 0.032 | 0.101 | 0.301 | 1.004 | 3.010 | 10.02 | 30.03 ms |

| RELEASE | .05 | .1 | .2 | .5 | 1 | 2 s | VAR 3 s |
|---|---|---|---|---|---|---|---|
| measured | 0.0523 | 0.1047 | 0.2094 | 0.5234 | 1.047 | 2.094 | 3.140 s |

## The CV-sum link in OLD (CvSumBranching; ADR-67's issue, fixed)

The 2500 sums the control voltages while "both channels are still detecting their own control voltages" (D §2.4
[V S6]): each channel's detector and timing make its own CV c, and each VCA follows the node r = (1 − w) c_own +
w c_other, w = k / (1 + k) (`LinkCvSum`). In OLD each detector senses its own OUTPUT, which the node sets:

    c'_ch = α c_ch + (1 − α) r̂_fb(x_ch − r'_ch),    r'_ch = (1 − w) c'_ch + w c'_other          (*)

ModeEngine's frozen FB step solves each lane alone, links the roots and commits the LINKED value as the next state
(K2 #5b): with a CV sum that re-links every sample, so a partial link settled as a full one (ADR-67: 99.9 % at 40 %).
CvSumBranching keeps each channel's CV as its state and solves the link inside the affine map (lead revision 4): given
the other lane's CV c'_o, a lane's applied GR is the root of ModeEngine's own solve with
A = (1 − w)(c − k c) + w c'_o and B = (1 − w) k, and its CV is (r' − w c'_o) / (1 − w). `solveFb` iterates the pair
(both lanes at once, Jacobi, E's predictor per lane per iteration) until no CV moves by more than 2⁻²¹ (1 + |c|) dB, at
most 12 iterations, and returns the CVs; the engine's `LinkCvSum` of them IS the node, so the applied GR is r'; `commitFb`
keeps the CVs. The iteration contracts by w B s / (1 + (1 − w) B s) < 1 per step (s = r̂_fb′). LINK IND runs
SmoothBranching's FB step bit for bit (sub-ulp carry included); NEW runs SmoothBranching's FF step on the linked target.

Through the engine (OLD, 4:1 MED, L a square at T + 12, R silent, settled), against the fixed point of (*):

| LINK | IND | 50 | 60 | 70 | 80 | 90 | 100 % |
|---|---|---|---|---|---|---|---|
| GR L / R (dB) | 9.00 / 0 | 8.00 / 4.00 | 7.83 / 4.70 | 7.66 / 5.36 | 7.50 / 6.00 | 7.35 / 6.61 | 7.20 / 7.20 |

R / L = w / (1 − w) = k: a 50 % link stays 50 %. The loud channel takes less GR than unlinked because its VCA follows
the node, and its loop raises its own CV to compensate (c_L = 12 dB at 50 %). Unit rows (20,000 random cases, 44.1–192
kHz, every ratio, knee and OLD attack, releases 50 ms–3 s, LINK 50–100 %): the applied GR within 6.7e-6 dB of the
system solved in double; 2.4 iterations on average from random states (1 when the pair is settled or idle, 2 solves as
SmoothBranching's), 0.4 % of those cases at the cap (still within 1e-5 dB).

## KNEE: HARD / MED / SOFT [H]

The 2500 publishes the three positions, not their widths. W = 0 (QuadKnee's hard knee, 1e-4 dB), 6 dB (the host
default width, so a Mode switch from Clean's default lands on MED) and 12 dB. In NEW W is the input-domain width; in OLD
it is at the output (QuadKnee's FB convention) and spans (1 + R) W / 2 at the input, which is the TYPEs' audible
difference below the asymptote.

## THRUST (the host side-chain tilt)

`sce` is stepped NORM 0 / MED 1.5 / LOUD 3.01 dB/oct on the host's 6-section tilt pivoted at 1 kHz (E §8, ADR-67):
LOUD = "a filter in front of the RMS detector, with a slope of 10 dB per decade" [V S6]; MED [C] (the manual: "slight
low frequency attenuation and a slight high frequency boost"; SOS reads MED 2 / LOUD 4 dB/oct, D §8.5) is half of
LOUD. The Mode adds no shaping of its own (`ScShape` Flat, `m[]` zero), so there is no double emphasis:
`analysis::scResponse` at LOUD reads −6.02 dB at 250 Hz and +3.03 dB at 2 kHz against 1 kHz.

## Time constants (ADR-63) and TIME MODE

The TimeSpecs declare the published times (`kit::attackFromView` / `releaseFromView`), which are the closed-loop times
in both TYPEs. NEW is open loop; under OLD `physical()` converts the attack to the loop's open-loop τ = t (1 + k),
k = `QuadKnee::loopGain(slope)` = R − 1 (FET 76's and Diode 609's conversion). Releases are not converted: the level
drops below the threshold and the loop opens. OLD on D2 at 4:1: attacks 0.038 / 0.108 / 0.309 / 1.011 / 3.02 / 10.03 /
30.03 ms, releases +1.3 % (0.0507 … 2.027 s); ∞:1 HARD 1.014 ms at the 1 ms position.

TIME MODE (`tmode`, its own slot) FIXED / VAR: RELEASE is the stepped switch under FIXED and the continuous pot
50–3000 ms (default 500) under VAR (a dependent list, 01 §10.7; D §2.4 [V S6]).

## AUTO makeup

The AUTO word on MAKEUP (`automu` MAN / AUTO) sets `kEngAutoMakeup`: the engine adds r̂(0 dBFS) of the current
THRESHOLD, RATIO and KNEE ("based on ratio and threshold", D §2.4 [V S6]; E §2.2 with k = 1 [H]). MAKEUP 0–24 dB, MIX
0–100 % (the 2500's crossfade law; its "parallel" law is dropped, D §5).

## Heuristic constants [H]

| Constant | Value | Where | Source and reason |
|---|---|---|---|
| RMS window per release | τw = τR / 50 (floor 1 ms) | `RmsCatch::kWindowPerRelease`, `kMinWindowMs` | the 2252's window is its fall rate (E §2.5c); 1/50 keeps a release's measured time within +4.7 % (D2) while the default and slow positions read a steady tone's RMS (10–60 ms windows) |
| Jump catch | 20 dB (×100) | `RmsCatch::kCatchRatio` | above any steady program's crest (Gaussian 10 σ); dsp.time's 40 dB steps and onsets from quiet reach the gain computer at once, so the published attacks hold (D2 within 7 % at .03 ms, 0.3 % from 1 ms) |
| Knee widths | 0 / 6 / 12 dB | `Bus25Desc.cpp` `kKnee` | unpublished; MED = the host default, SOFT = twice it |
| THRUST MED | 1.5 dB/oct | `kThrust` | [C] between the manual's words and SOS's 2 dB/oct; half of LOUD's 10 dB/decade |
| Auto makeup | r̂(0 dBFS) (k = 1) | engine (`kEngAutoMakeup`) | E §2.2; the manual's "based on ratio and threshold" |
| Coupled solve | 2⁻²¹ (1 + \|c\|) dB, ≤ 12 iterations | `CvSumBranching::kTolRel`, `kMaxIters` | float precision of the CV; the cap bounds the worst sample (a fast attack's onset at ∞:1) |

## Internals (UiFrame)

On the channel whose applied GR is larger: **RMS DET** (the detector's level, after THRUST), **OWN CV** (the channel's
CV before the link: the loop's CV state in OLD; in NEW, which links the targets, its gain computer's target at RMS DET),
**LINKED CV** (the applied GR after the node; the history internal).

## Verification

| Probe | What it holds Bus 25 to |
|---|---|
| `dsp.bus25topo` | the TYPE flip (click metric at ECO / STD / HQ, mono and stereo, pre-edge identity, landing); the CV-sum solve (unit rows vs the coupled system in double, IND = SmoothBranching FB bit for bit, NEW = its FF bit for bit, mono = the uncoupled root, the settled law through the engine for every LINK step); RmsCatch (sine RMS, the catch at 40 / 12 dB, monotone, noise never catches, the window's fall rate per release, seed round trip); OLD's attacks and releases (ADR-63) and VAR's pot; THRUST (exact dB/oct, neutral m[], flat Mode shaping, scResponse at LOUD) |
| `dsp.static.bus-25` | every ratio × two thresholds × three knees, the textbook formula, fidelity blocking; OLD as the voice rows (the FB curve) |
| `dsp.time.bus-25` | every ATTACK and RELEASE detent against the published times, meter truth per sample, GR OFF, the carry, the Quality rows |
| `dsp.link.bus-25` | the CV-sum law in NEW (every LINK step) and OLD's structural rows (bounds, settled, swap, mono, k = 1) |
| `dsp.zipper.bus-25` | every detent edge click-free, the TYPE detents through the kernel crossfade |
| `dsp.srsweep`, `dsp.analysis`, `dsp.hostile`, `dsp.null`, `dsp.switch`, `dsp.quant`, `dsp.rt`, `dsp.print`, `dsp.latency`, `dsp.registry`, `proc.*`, `ui.*` | the generic per-Mode rows, fidelity blocking |

## Known limits

- No colour: the 2510/2520 discrete op-amps and the output transformer (D §2.4) are `ColourNone` (the card's
  deliverables do not include them; the COLOUR view draws the identity).
- OLD with a partial or full link solves (*) on absolute roots: SmoothBranching's FB sub-ulp carry (S10) does not extend
  to the coupled pair, so a release slower than ~2¹⁴ samples may stop up to ulp(r) / (2 c) short of a non-zero target
  (0.07 dB at 3 s and 48 kHz, 0.28 dB at 192 kHz; a release to 0 dB does not stall). LINK IND keeps the carry.
- The node weight w is designed on control ticks (every 16 samples): for up to one tick after a LINK change the OLD
  solve assumes the previous node (a stepped switch; no state steps).
- The ADR-63 conversion uses the loop gain above the knee: at ∞:1 with MED or SOFT the output knee moves OLD's
  equilibrium and the 1 ms attack measures 1.16 ms on D2 (16 %; HARD 1.01 ms).
- NEW links the gain computers' targets before the ballistics (01 §5.2, E §8); the hardware sums the CVs after the
  timing, which differs only while the two channels' ballistics take different branches.
- The 2500+'s link SHAPE (FAST / SLOW / BOTH) and the original 2500's link filters are not modelled (D §2.4).
- THRUST pivots at 1 kHz (the host tilt); the 527's published "±15 dB at 20 Hz / 20 kHz" pivots near 630 Hz [C].
- `ctBudgetNsPerSample` (45) is DW's; `fcmp_bench` was not run (other agents were building). OLD with a link costs
  about 1–2.4 coupled iterations (2–5 QuadKnee solves) per sample against SmoothBranching's 2.

## Revision history

| Revision | Sprint | Change |
|---|---|---|
| 1 | S11 (M6) | Full traits (01 §10.7), provisional cleared: RmsCatch (release-tied window, jump catch), CvSumBranching (the CV-sum link solved inside the OLD loop, ADR-67), ADR-63 attacks in OLD, THRUST on the host tilt, knee widths 0 / 6 / 12. Not shipped (no `modes-ever.tsv` row), so no bump. |
