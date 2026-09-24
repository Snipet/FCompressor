# Opto 2A (slot 3, `opto-2a`)

Mode sheet (01 §8.4 step 6): what Opto 2A runs, the T4 cell model, every heuristic constant with its source and fit,
and the revision history. Owned by the Mode's card (M3 in S9). The contracts are 01 §10.6 (descriptor, traits) and
01 §5.2–5.3 (policies, engine); the code is `Source/fcdsp/modes/opto-2a/` and the policy headers named below. The unit
is D §2.2's Teletronix LA-2A (a T4B optical attenuator in a feedback loop, a tube line amplifier and transformers).

## Identity

| Field | Value |
|---|---|
| Key, slot | `opto-2a`, 3 (`Modes.def`) |
| Group, rigor, family | opto, `Rigor::character`, `CurveFamily::custom` |
| Topology | feedback only (`kTopologies = FB`); an external key runs the curve feed-forward (E §2.6) |
| Detector law | `custom`: the curve's x axis is the drive's peak (a sine, a square and DC of one peak read alike) |
| Revision | 1 |
| Provisional | no (cleared by M3, S9: its fidelity rows are blocking spec rows and its goldens can be blessed) |

## Traits (Opto2A.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `OptoSense` | DETECT locked T4 CELL: the EL panel's drive, rectified, held peak |
| Computer | `FeedbackDelayed<OptoCellCurve>` | PEAK RED. 0–100 (+20…−50 dBFS), COMP / LIMIT (S = 2/3, 0.9; loop gain k = 2, 9), KNEE n/a (the cell's) |
| Link | `LinkMax` | DUAL / LINK (link 0 / 1), after the per-lane solve (K2 #5b) |
| Ballistics | `OptoCell` | ATTACK locked ~10 ms, RELEASE locked 60 ms to 50 % (both program-dependent); the slow part's constants in `m[2..4]` |
| Stage 2 | `NoStage2` | – |
| Colour | `TubeTransformer` | VOICE locked TUBE; DRIVE ±24 dB (the standard extension) |
| SC shape | `R37Shelf` | EMPHASIS 0–10 (the R37 set-screw): a low shelf, 0…−10 dB below 1 kHz, `m[0]` |

`EngineParams::m`: `m[0]` R37 amount (dB), `m[1]` 1 in LIMIT (01 §10.6; informational, the law change is the loop
gain), `m[2]` β, `m[3]` τ_m (ms), `m[4]` τ_s (ms). `physical()` hands the host's `sce` tilt 0 dB/oct (below).

## The T4 cell model

Per lane, per sample; x = the detector level (dB), T = `thrDb`, S = `slope`, k = S / (1 − S) (QuadKnee.h's FB
convention).

**Sensor (`OptoSense`).** The EL panel is driven by the output through the side chain; the panel's light follows the
rectified drive with its persistence. The level l = 20 log10 |v| (after the R37 shelf) is held: a new peak sets it and
arms a 5 ms hold, a sample within 0.1 dB of it re-arms a running hold (the sampled crests of a tone the rate does not
divide), and after the hold it falls at 20 log10(e) / 5 ms = 1.74 dB/ms (the afterglow) until it meets the drive
exactly. The hold bridges half-waves down to 100 Hz, so a steady tone's light is flat; below 100 Hz it dips between
half-waves (3.3 dB peak to peak at 50 Hz: the frequency-dependent ripple of a real cell, D §2.2).

**Curve (`OptoCellCurve`, `law::LdrShunt`).** The photoresistor shunts the signal behind a series resistor: gain
1 / (1 + g), GR r = 20 log10(1 + g), g the normalised conductance. Under a steady light g settles on drive^k, drive =
10^((y − T) / 20) at the sensed output level y: E §2.7's light law with no light threshold (e0 = 0) and one exponent for
panel and photoresistor. So the loop's curve is r̂_fb(y) = 20 log10(1 + 10^(k (y − T) / 20)), a softplus in dB whose
asymptote k (y − T) makes R_eff = 1 + k: COMP 3:1, LIMIT 10:1 (D §2.2). The exponent IS the LIMIT switch's "EL-panel
drive law change". Its slope never exceeds k (FeedbackDelayed's guard assumes it). The static FB curve (staticGr, the
TRANSFER view) solves r = r̂_fb(x − r):

| x − T (dB) | −30 | −20 | −10 | −5 | 0 | +5 | +10 | +20 | +30 |
|---|---|---|---|---|---|---|---|---|---|
| COMP GR (dB) | 0.01 | 0.08 | 0.71 | 1.69 | 3.32 | 5.52 | 8.11 | 13.98 | 20.29 |
| LIMIT GR (dB) | 0.00 | 0.00 | 0.00 | 0.05 | 1.57 | 5.19 | 9.36 | 18.12 | 27.04 |

The local ratio of COMP is 1.6:1 at T, 2.2:1 at T + 10 and 2.6:1 at T + 20 (3:1 asymptotically): D §2.2's "very soft"
knee; LIMIT reaches 6.9:1 at T + 10 and 8.9:1 at T + 20.

**Loop (`FeedbackDelayed<OptoCellCurve>`).** The naive one-sample-delay loop E §2.6–2.7 prescribes for the cell: the
light of sample n comes from the output of sample n − 1, r = α r₁ + (1 − α) r̂_fb(x − r₁). Its runtime guard (K2 #5c)
compares k with α / (1 − α) of the attack pole at the actual rate; at the cell's open-loop attack the bound is 661
(COMP) and 2,205 (LIMIT) at 22.05 kHz, 1,440 / 4,800 at 48 kHz, so the delayed loop always runs; a violated bound
(a 20 µs attack forced by `dsp.optocell`) falls back to FeedbackZdf per sample, bit for bit.

**Ballistics (`OptoCell`).** Two parts, in the GR domain, with u = the light (the curve's value at the sensed point):

    fast   r_f <- smooth-branching(u)        attack tau_on, release tau_f           (SmoothBranching's FB maps)
    slow   s   <- one-pole toward beta u     tau_m while beta u > s (the memory builds), tau_s otherwise
    out    r    = max(r_f, s)                the fast path is held at the slow part while it holds the GR

The slow part is the memory: it charges only while the light is on, and only at τ_m, so its level after a passage is
the program's history. When the light goes off the fast path releases to the slow part's level, which lets go at τ_s.
Because the slow part never exceeds β = ½ of the light it charges toward, the GR passes 50 % on the fast path:
"60 ms to 50 %" holds whatever the history, and the rest takes from a fraction of a second to 13 s. The attack moves
the fast path only, so the static curve settles at the attack's speed and the memory never makes the GR creep.

The light for the slow part is read through a third solve of the map {r₁ / 2, ½}, whose delayed root is (r₁ + u) / 2 at
the same sense point (u to an ulp; recovering it from the fast root, (r − A) / B, divides the root's rounding by B and
biased the tail's slow part by 1e-4 dB). The link acts on the solved GR (LinkMax, K2 #5b); the committed r becomes the
fast path's state and the slow part never exceeds it.

**Why two states, not E §2.7's three.** E's model keeps fast and slow conductances plus a memory term m that sets the
slow release (τ = 0.3 + 3.2 m s). The frozen `Carry` (01 §5.3) hands a ballistics policy four lanes: the channels' GR
and two aux lanes. OptoCell packs `{r₀, r₁, s₀, s₁}` into `Carry::grDb`, so a seeded engine continues the old one's
cell within `dsp.time`'s 2-ulp rule (the slow part's sub-ulp remainder is not in `Carry`), in the fast phase and in the
memory's tail. A third state per channel would not fit; the memory is therefore the slow part's slow charge. A
hand-over from another Mode brings that Mode's aux GR in lanes 2–3: the slow part starts charged (s = min(aux, r)), so
the GR is kept and let go no faster than the memory would (DualRelease's precedent).

**Colour (`TubeTransformer`).** f(u) = tanh u − 0.1 tanh² u, u = k x, k = (1/64) · 10^(DRIVE / 20), level-compensated
(y = x + (f(u) − u) / k): tanh the transformers' odd saturation (through ADAA-1, E §2.9), the even term the
single-ended tube stages (per sample: a quadratic aliases only above fs / 4, and ADAA-1's segment average would smear a
loud sample's even residual into the next, quiet one). k glides linearly across each call (DRIVE moves per control
tick), so the even term never steps. No DC blocker: its memory would release a loud passage's DC into the quiet one
after it.

**Emphasis (`R37Shelf`, S9 lead revision 4).** H(s) = (s + G ω_c) / (s + ω_c), G = 10^(−E / 20), f_c = 1 kHz, E = 0…10 dB
(EMPHASIS 0…10): unity above 1 kHz, −E dB below G f_c (−10 dB under 316 Hz at E = 10). TPT one-pole, E smoothed per
tick (20 ms), an exact bypass at E = 0. `EMPHASIS` used to drive both the host's `sce` tilt and `m[0]` (a double
emphasis, F7's finding); `physical()` now sets `sceDbOct = 0`, so the detector path (`analysis::scResponse`) is the
R37 shelf alone (`dsp.optocell`'s `r37.emphasis.*` rows).

## Heuristic constants [H]

| Constant | Value | Where | Source and fit |
|---|---|---|---|
| PEAK RED. law | 0…100 ↔ +20…−50 dBFS, 0.7 dB/unit | `Opto2ADesc.cpp` `kPrTopDb`, `kPrDbPerUnit` | fitted (01 §12 #5; 01 §10.6 had +24…−40): PR 0 leaves a 0 dBFS peak at 0.08 dB of GR ("0 = no GR", D §5.2); the default PR 40 (−8 dBFS) holds 0 VU program's −6 dBFS peaks near 4 dB of GR, the LA-2A's usual working range (01 §10.6's law held them under 2 dB) |
| GAIN law | 0…100 ↔ −10…+30 dB (default 40 = +6 dB) | `gainDial`/`gainPlain` | 01 §10.6, unchanged |
| EMPHASIS law | 0…10 ↔ 0…10 dB of R37 | `emDial`, `physical()` | 01 §10.6 ("up to 10 dB", D §2.2) |
| Light law | g = drive^k, no light threshold, r = 20 log10(1 + g) | `OptoCellCurve.h` | E §2.7 (hyperbolic shunt law; γ_EL·γ_LDR fitted to the published ratios 3:1 / 10:1, D §2.2) |
| R_series | 10 kΩ | `law::LdrShunt::kSeriesKohm` | the LDR internal only (R_series / g; 1.1 kΩ at 20 dB, 82 kΩ at 1 dB, 1 MΩ dark, E §2.7's VTL5C figures) |
| Panel hold | 5 ms (bridges half-waves down to 100 Hz) | `OptoSense::kHoldMs` | the panel's persistence; a steady tone's light is flat (the static curve holds a sine's peak) |
| Panel afterglow | 5 ms (1.74 dB/ms) | `OptoSense::kReleaseMs` | short enough that the light lags a dropped drive by ~9 ms only |
| Near-peak refresh | 0.1 dB | `OptoSense::kRefreshDb` | the sampled crests of 1 kHz at 22.05 kHz differ by 0.09 dB |
| β (slow share) | ½ | `kSlowShare` → `m[2]` | E §2.7's w = 0.5; keeps "50 % in 60 ms" on the fast path |
| τ_m (memory charge) | 5 s | `kMemoryChargeMs` → `m[3]` | E §2.7's "memory 5 s up" |
| τ_s (slow release) | 5 s | `kSlowReleaseMs` → `m[4]` | fitted to "then 1–15 s" (D §2.2): the tail after ≥ 10 s of GR returns to 0.5 dB in 12.8 s |
| Attack loop factor | τ_on = 10 ms × (1 + 1.0 k): 30 ms COMP, 100 ms LIMIT | `kAttackLoop` | ADR-63 (published = closed loop); fitted: `dsp.time` measures 10.7 ms (COMP), `dsp.optocell` 10.1 ms (LIMIT) |
| Release fit | τ_f = 86.6 ms × 0.85 = 73.6 ms | `kReleaseFit` | the published t50 of 60 ms includes the panel's hold and afterglow (~9 ms): `dsp.time` measures 60.0 ms |
| Colour input | k = 1/64 at 0 dB drive | `TubeTransformer::kIn` | set by meter truth (below), not by the unit's THD |
| Even term | 0.1 | `TubeTransformer::kEven` | H2 the larger harmonic at every level (tube, D §2.2 "low-order") |
| Tail | 25 s (5 τ_s) | `optoTail` | the memory's tail (01 §10.6 had 15 s) |

## Measured (`dsp.optocell`, 48 kHz, COMP, PR 40, a 1 kHz square burst at T + 20, then T − 20)

| Burst | 0.1 s | 0.5 s | 1 s | 5 s | 10 s |
|---|---|---|---|---|---|
| GR at the end of the burst | 13.98 dB | 13.98 dB | 13.98 dB | 13.98 dB | 13.98 dB |
| Release to 50 % | 60.0 ms | 60.0 ms | 60.0 ms | 60.0 ms | 60.0 ms |
| Release to 10 % | 0.18 s | 0.18 s | 0.18 s | 5.8 s | 7.4 s |
| Release to 0.5 dB | 0.26 s | 1.8 s | 5.0 s | 11.2 s | 12.8 s |
| MEMORY at the end | – | 9.9 % | – | 63 % | – |

Attack (63 % of the GR change, expDb): 10.7 ms COMP, 10.1 ms LIMIT (published ~10 ms average; the program range 5–20
ms). `dsp.time`: attack 10.68 ms, release 59.97 ms to 50 %, both inside the kit's program bands; `dsp.srsweep`: the
same times within 0.08 % from 22.05 to 384 kHz, the curve within 0.003 dB of 48 kHz's. The static curve against the
declared one on a 1 kHz sine (`dsp.static`, COMP and LIMIT at T = −30 and −10 dBFS): ≤ 0.001 dB (tolerance 0.5 / 0.75
dB); the ratio and threshold rows within 0.002; tap against audio ≤ 0.0003 dB.

Telemetry (the EFF readouts of the locked, program-dependent ATTACK and RELEASE slots): the attack is the closed loop's
at the operating point, τ_on / (1 + k L) with L = 1 − 10^(−u / 20) the light's share (about 11 ms at 14 dB of GR, 15 ms at
4.7 dB, τ_on with the light off); the release is τ_f (51 ms to 50 %: the published 60 ms less the panel's delay) while
the fast path holds the GR, τ_s (3.5 s to 50 %) through the memory's tail.

## Meter truth and the colour's level

VOICE is locked, so every probe runs through the colour stage, and `dsp.time`'s tap-vs-audio row (character: ≤ 0.1 dB
at every sample) reads the colour's gain y / x on a square whose first samples, before the cell attacks, sit at the
input threshold + 20 dB (+12 dBFS at PR 40). The colour's input scale and even term are set there: at that onset
u = 0.062, the gain moves by u² / 3 + 0.1 u = 0.065 dB (`dsp.time` reads 0.064 dB); where the square later drops
40 dB, ADAA-1's odd residual averaged across the step adds 0.011 dB. At 0 dB of drive the second harmonic of a sine is
−62 dB at 0 dBFS and −80 dB at 0 VU, the third −94 dB at 0 dBFS; the even term's DC is −62 dBFS for a 0 dBFS sine. The
LA-2A's own distortion (about −60 dB at 0 VU, H2) needs about +18 dB of DRIVE (H2 −62 dB at 0 VU, −44 dB at 0 dBFS).

## Verification

| Probe | What it holds Opto 2A to |
|---|---|
| `dsp.static.opto-2a` | D1 at 1 kHz: COMP and LIMIT × thresholds {−30, −10}: the declared curve, ratio, threshold, meter truth (all ≤ 0.002 dB); the static FB curve against bisection (≤ 1e-5 dB). The per-sample FB branch rows are NOTE lines for a program-dependent Mode (they assume the published one-poles); `dsp.optocell` holds the cell's maps |
| `dsp.time.opto-2a` | attack and release in the program bands; GR OFF ramp (see below); the carry hand-over (the cell's two states, exactly); telemetry and internals |
| `dsp.srsweep.opto-2a` | curve and times across rates; the 22.05 kHz stability rows (K2 #5c) |
| `dsp.optocell` | the curve against the law, the guard (bound, fallback bit-identity, 22.05 kHz stability with the delayed loop and with the fallback), the cell's per-sample FB recurrence in double (≤ 1e-5 dB over attack, fast release and the slow tail), the program dependence above, carry in the tail, the sensor, the R37 shelf's implementation and the double-emphasis fix, the colour's meter-truth figures |
| `dsp.link`, `dsp.analysis`, `dsp.hostile`, `dsp.null`, `dsp.switch`, `dsp.zipper`, `dsp.quant`, `dsp.rt`, `dsp.print`, `dsp.latency` | the generic per-Mode rows, fidelity rows blocking |

GR OFF. The steady output of Opto 2A is exceptionally clean (the held light leaves no GR ripple: the click metric's
controls sit at −131 dB, against −119 dB for Bus G and Clean), which exposed the rounding of `ModeEngine`'s
smootherstep near 1 (the cancellation of 10 − 9: ±5e-7, ±3.5e-6 dB on 7 dB of GR, +5.5 dB on `time.groff.off`). The
shape is now evaluated from the nearer end, as `host/Ramps.h`'s `blend()` already was (`ModeEngine.h`, an ownership
deviation of M3; −5.1 dB on that row, no golden moved).

## Known limits

- **Memory granularity.** The memory is the slow part's charge (two states per channel, above). A transient shorter
  than about 0.3 s leaves almost no memory, so it releases fully in about 0.26 s; E §2.7's three-state model would give
  it a ~1.5 s tail. A `Carry` that seeds a ballistics policy with more than four lanes (or `ModeEngine::seed` passing
  `Carry::relNowMs`, "lets a program-dependent Mode seed its memory term", to the ballistics) would allow it.
- **Detector law.** A real cell averages its light, so a sine and a square of one peak get slightly different GR; the
  held-peak sensor reads both (and DC) alike, as the probes' single x axis needs. Below 100 Hz the light ripples.
- **FB sub-ulp (ADR-66).** The fast path's root is absolute: a partial release toward a non-zero light stops about
  ulp(r) / (2 c_f) short of it (`dsp.optocell`, 14 → 8.1 dB: +0.001 dB at 48 kHz, +0.012 dB at 384 kHz); a release
  toward silence does not stall. The slow part is the policy's own recurrence and carries its remainder (no stall).
- **Colour level.** Set by the meter-truth row above, about 20 dB under the unit's distortion at 0 dB of DRIVE.
- **Hand-over into Opto 2A** from another Mode starts with the slow part charged (s = the carried GR): the GR comes down
  at the memory's pace (`dsp.switch`'s over-compression allowance covers it).

## Revision history

| Revision | Sprint | Change |
|---|---|---|
| 1 | S9 (M3) | Full traits (01 §10.6), provisional cleared; the T4 cell (OptoSense, OptoCellCurve, FeedbackDelayed, OptoCell), R37Shelf, TubeTransformer; [H] constants fitted (PEAK RED. law, attack loop factor, release fit, τ_s). EMPHASIS no longer drives the host tilt (S9 lead revision 4). Not shipped (no `modes-ever.tsv` row), so no bump. |
