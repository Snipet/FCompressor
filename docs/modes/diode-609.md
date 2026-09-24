# Diode 609 (slot 5, `diode-609`)

Mode sheet (01 §8.4 step 6): what Diode 609 runs, every heuristic constant with its source and reason, and the
revision history. Owned by the Mode's card (M5 in S10). The contracts are 01 §10.7 (the sketch), 01 §5.2–5.3
(policies, engine, the feedback step, the S10 interface revision) and ADR-62 / ADR-63 / ADR-64; the code is
`Source/fcdsp/modes/diode-609/` and the policy headers `stages/stage2/SharedElementMax.h`,
`stages/scshape/SlowHp.h` and `stages/colour/DiodeBridge.h`. The unit is D §2.5's Neve 33609 (the 2254's steps where
the 33609's are unverified): a compressor and a limiter, both feedback, sharing one diode-bridge gain element.

## Identity

| Field | Value |
|---|---|
| Key, slot | `diode-609`, 5 (`Modes.def`) |
| Group, rigor, family | diode, `Rigor::character`, `CurveFamily::custom` (the progressive knee) |
| Topology | feedback only (`kTopologies = FB`); an external key runs the compressor feed-forward (E §2.6), see Known limits |
| Detector law | `peak` (after the gain element) |
| Stage 2 | `Stage2Kind::sharedElementMax`: the limiter, drawn on the transfer curve (`ModeEntry::staticS2`) |
| Revision | 1 |
| Provisional | no (cleared by M5, S10: its fidelity rows are blocking spec rows and its goldens can be blessed) |

## Traits (Diode609.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `PeakLog` | DETECT locked PEAK · FEEDBACK |
| Computer | `QuadKnee` (FB closed form) | THRESHOLD −20…+10 dBu /2, RATIO 1.5 / 2 / 3 / 4 / 6, KNEE derived from RATIO (below) |
| Link | `LinkMax` | LINK DUAL / STEREO, after the per-lane solve (K2 #5b); the limiter is linked by the same law |
| Ballistics | `AutoSwitch<SmoothBranching, DualReleaseT<1, 2, 3>, CompAuto>` | ATTACK FAST / SLOW (3 / 6 ms closed loop), RELEASE .1 / .4 / .8 / 1.5 s + A1 / A2 |
| Stage 2 | `SharedElementMax<PeakLog, AutoSwitch<SmoothBranching, DualReleaseT<4, 5, 6>, LimitAuto>>` | LIMIT THRESHOLD +4…+15 dBu /1 + OFF, LIMIT ATTACK FAST / SLOW (2 / 4 ms), LIMIT RELEASE 50 / 100 / 200 / 800 ms + A1 / A2 |
| Colour | `DiodeBridge` | VOICE locked DIODE; DRIVE ±24 dB (the standard extension) |
| SC shape | `SlowHpT<0>` | ATTACK SLOW: a first-order 100 Hz high-pass on the compressor's side chain only |

`EngineParams::m` (ADR-64: A1 / A2 tag both release switches, and `tags` is one OR, so each switch's AUTO position has
its own slots and each `AutoSwitch` selects on them): `m[0]` the SLOW corner (100 Hz; 0 = FAST); `m[1..3]` the
compressor's A1/A2 DualRelease τRf, τC, τRs (ms; 0 on the manual positions); `m[4..6]` the limiter's. `m[7]` is free.

## The shared element (SharedElementMax)

The 33609 combines the outputs of both side chains before the gain element ("both stages … are feed-back designs";
"outputs of both side-chains are combined before feeding the gain-control element", D §2.5 [V S11]), so the element
takes the larger control and both side chains sense the element's output:

    r  = max(r1, r2)                                   per channel (lanes 0-1)
    r2 = the limiter's own loop, on the aux lanes      r2 = A + B r̂2_fb(x − max(r1, r2)) per ballistics branch

- ModeEngine's frozen FB step solves the compressor (stage 1, self-sensed: x − r1), links it and commits it, then hands
  `combine()` r1 and the detector level x. The host feeds the detector's aux lanes a copy of the channels
  ({c0, c1, c0, c1}, Router.h), so x's lanes 2–3 are the limiter's detector (the same PeakLog; the traits assert it).
- The limiter's sense point is the SHARED output: where its self-sensed root is below r1 the compressor holds the
  sense point and the root is the explicit A + B r̂2(x − r1). Both cases in one solve functor, so SmoothBranching's
  predictor, DualRelease's max of roots and the FB sub-ulp carry apply unchanged; `dsp.sharedelement` holds 20,000
  random cases to 200-step bisection within 1e-5 dB. Consequence: the limiter does not charge while the compressor keeps
  the output under the limit (LIMIT GR ≤ COMP GR, LIMIT WINS 0: `mode.shared.*`).
- Limiter law [U → H]: `QuadKnee` in FB at the smoothed stage-2 threshold, S = 0.99 (loop gain k2 = 99, 100:1: D §2.5
  gives ">100:1" [U]; QuadKnee's FB clamp), knee **kKneeDb = 0.5 dB** at the output [H] (a diode limiter is not a brick
  wall, but its output sits at the threshold). Measured: 20 dB over +10 dBu, the output sits +0.15 dB over the limit.
- OFF (`s2thr` = 24 = kS2Off, never kTagOff, ADR-62): ModeEngine fades stage 2 with `s2On_` (20 ms smootherstep) and
  smooths the threshold in [−40, 24]. While the smoothed threshold rests at 24 the limiter is out: its state rests at
  0 dB and `combine()` returns r1 (NoStage2's arithmetic), so an OFF limiter never charges and costs no solve. On/off
  measured: the element's largest per-sample move across both edges 0.048 dB (a 7 dB limiter swing); 25 ms after the
  OFF edge the GR is the compressor's alone, bit for bit (`mode.s2on.*`).
- Static form (S10 lead revision 4): `combineStatic = max(r1, r2_static(x))`, which is what the engine settles on in
  both sense models; `ModeEntry::staticS2` points at it, so `analysis::staticGain` (the TRANSFER curve) draws the
  limiter's segment. The settled engine matches it within 1e-4 dB at 11 levels with each stage deciding somewhere
  (`mode.s2.settled_max_err_db`).
- Link: the Mode's LINK (DUAL / STEREO) links the limiter's two channels by LinkMax's law after its solve, before its
  commit ("both channels always compressed by the same amount", D §2.5 [V S10]); STEREO gives the silent side the
  limiter's GR exactly, DUAL none.

## KNEE: the progressive curve (fitted)

D §2.5: the 33609 compressor is "soft and progressive, so the true ratio is only attained >5dB above the threshold";
the 2254's knee is "fairly soft … over a 10dB range". QuadKnee's FB knee W lives at the output, so in input dB it spans
T − W/2 … T + (1 + k) W/2, a span of W (1 + R) / 2. DW's locked 5 dB knee reached the ratio 3.75 (1.5:1) … 15 dB (6:1)
over the threshold. Now the knee is **derived from RATIO**: W = 20 / (1 + R), so the input knee spans
**10 dB at every ratio** and the true ratio is reached 6 (1.5:1) … 8.6 dB (6:1) over the threshold:

| RATIO | S | loop gain k | W (output) | input knee | ADR-63 attack τ (FAST / SLOW) |
|---|---|---|---|---|---|
| 1.5 | 0.3333 | 0.5 | 8.00 dB | T − 4.0 … T + 6.0 | 4.5 / 9 ms |
| 2 | 0.5 | 1 | 6.67 dB | T − 3.3 … T + 6.7 | 6 / 12 ms |
| 3 | 0.6667 | 2 | 5.00 dB | T − 2.5 … T + 7.5 | 9 / 18 ms |
| 4 | 0.75 | 3 | 4.00 dB | T − 2.0 … T + 8.0 | 12 / 24 ms |
| 6 | 0.8333 | 5 | 2.86 dB | T − 1.4 … T + 8.6 | 18 / 36 ms |

`dsp.static` (1 kHz sines, character tolerances): the measured ratios 1.497 / 1.991 / 2.964 / 3.917 / 5.762 against
1.5 / 2 / 3 / 4 / 6 (≤ 8 %), curve error ≤ 0.27 dB (the sine's peak held through the loop, not a square's), every FB
configuration's static curve against bisection ≤ 1e-5 dB.

## Time constants (ADR-63)

The TimeSpecs declare the published closed-loop times; `physical()` converts each attack to its loop's open-loop τ,
**τ = t_published × (1 + k)**: the compressor's k = R − 1 (table above), the limiter's k2 = 99 (so FAST 2 ms → 200 ms,
SLOW 4 ms → 400 ms). Releases are not converted (the level drops under the threshold and the loop opens).

Measured through the engine (48 kHz): compressor attack FAST **3.005 ms** at 2:1 (`dsp.time`, 20 dB step); SLOW 6.55 ms
(its high-pass changes the square's detection, below); limiter attack **2.011 / 4.011 ms** (published 2 / 4); limiter
releases 50 / 100 / 200 / 800 ms within 5 %; manual compressor releases exact (0.1 / 0.4 / 0.8 / 1.5 s).
`EngineTelemetry::attackNowMs` reports the loop's open-loop τ (FET 76's convention).

## RELEASE A1 / A2 (DualRelease in the FB solve)

"a1 = 100 ms/2000 ms and a2 = 50 ms/5000 ms (self-adjusting)" (D §2.5 [V S10]): DualRelease's fast release τRf and slow
release τRs, both release switches (the limiter's a1/a2 are the same circuit's). Inside the FB solve: the max of the
fast and the slow root (01 §5.3), with the exact r̂ commit (X10: `QuadKnee::rhatFb` → `AutoSwitch` →
`DualRelease::commitFb(c, s, r, rhat)`). The release TimeSpec of A1 / A2 is program-dependent with the published span
[fast, slow] (A1 0.1–2 s, A2 0.05–5 s; `diodeRelease`).

| Constant | A1 | A2 | Source |
|---|---|---|---|
| τRf (fast release) | 100 ms | 50 ms | [V S10] |
| τRs (slow release) | 2000 ms | 5000 ms | [V S10] |
| τC (slow path charge) | 500 ms | 500 ms | **[H]**: unpublished. How long program must hold the GR before the recovery turns slow: a drum hit (< 0.1 s) recovers fast, a sustained phrase slowly. The 2254's "attack adjusted automatically along with the release" is not modelled. |

Measured (`mode.a1/a2.release.*`, 1 kHz square at T + 20 then T − 20, 1/e of the tapped GR): A1 **0.100 s** after a
20 ms burst, **2.098 s** after 3 s; A2 **0.050 s** / **5.038 s**. At `dsp.time`'s D2 configuration (0.5 s hold) A1 reads
1.27 s and A2 2.82 s, inside their spans.

## ATTACK SLOW: the side-chain high-pass (SlowHp)

SLOW "adds reduced sensitivity to <100Hz signals" (D §2.5 [V S10]): a first-order 100 Hz high-pass (TPT, prewarped) on
the compressor's side-chain lanes (0–1) only; the limiter's side chain (aux lanes) is not shaped. The switch fades the
filter in and out (a 20 ms per-tick amount, landing exactly), and FAST is an exact bypass. `magDb` is the digital
response (−3.01 dB at 100 Hz), which `analysis::scResponse` draws. Measured (T + 12, 2:1): a 50 Hz tone's GR 5.82 dB at
FAST, 2.36 dB at SLOW; at 1 kHz 5.82 / 5.70 dB (the slower attack); the limiter's GR on a 50 Hz tone is the same at
FAST and SLOW (`mode.slow.*`). On `dsp.time`'s 1 kHz square the high-pass droops each half period (τ = 1.6 ms), so
the GR ripples within the period: that probe judges SLOW's reversals once per period (M5's change, below).

## Colour: DiodeBridge [H]

The stage runs on the wet signal after the element. FetColour.h's two bounded bases of u = x / 2, an even
tanh²(u) (second harmonic, never the fundamental) and an odd tanh(u) − u (third harmonic, a soft compression), with
weights

    E = s(GR) e_B + e_T,   O = s(GR) o_B + o_T,   s(GR) = 1 − 10^(−GR/20)   (the bridge's share of the divider)

| Coefficient (x-domain) | Value | Reason |
|---|---|---|
| bridge even a2 | 0.020 | the bridge's imbalance: THD 0.30 % at +14 dBu with 10 dB of GR (D §2.5: "under 0.45 % with the limiter in") |
| bridge odd a3 | 0.002 | the balanced bridge's third; bounded by `dsp.time`'s per-sample meter truth after the 40 dB drop (ADAA-1's half-sample carry of the odd part: 0.119 dB at 0.004, 0.073 dB here; limit 0.10) |
| transformer even a2 | 0.0018 | THD 0.066 % at +20 dBu without GR (D §2.5: "THD 0.075 % in bypass") |
| transformer odd a3 | 0.0008 | the class-A amplifier's third |

Measured (`bridge.*`): at amp 0.4 and 10 dB of GR, H2 −50.4 dB, H3 −81.4 dB (the Taylor estimate within 0.25 dB); the
fundamental loses 0.023 dB at dsp.static's highest wet level (+3.3 dBFS, 7.7 dB of GR); digital silence stays silent;
block splits are bit-exact. `colourStatic = false` (the COLOUR view draws `transfer()` at the operating GR).

## Internals (UiFrame)

On the channel whose element GR is larger: **COMP GR** (the compressor's own GR, before the max), **LIMIT GR** (the
limiter's GR × the settled `s2On_` amount, what `s2GrDb` reports; the history internal), **LIMIT WINS** (1 while the
limiter holds the element).

## Verification

| Probe | What it holds Diode 609 to |
|---|---|
| `dsp.sharedelement` | SharedElementMax (OFF = NoStage2, the shared-sense FB solve vs bisection, aux lanes, link, static form, carry), SlowHp (bypass, aux lanes, response vs `magDb`, the fade), DiodeBridge (share law, transfer, silence, block splits, harmonics vs DFT, the published THD figures, the meter-truth budget), and the Mode: the stage-2 curve vs `staticGain`, the ceiling, the limiter's attack/release, the on/off fade, the shared sense, link, A1/A2 program dependence, SLOW, internals |
| `dsp.static.diode-609` | every ratio × two thresholds, fidelity blocking (ratio, threshold, curve), the FB curves vs bisection |
| `dsp.time.diode-609` | FAST / SLOW and every release detent (A1 / A2 in their spans), meter truth per sample, the carry, the Quality row |
| `dsp.srsweep`, `dsp.link`, `dsp.analysis`, `dsp.hostile`, `dsp.null`, `dsp.switch`, `dsp.zipper` (detent edges of `atk`, `s2thr` incl. OFF, `s2atk`, `s2rel`), `dsp.quant`, `dsp.rt`, `dsp.print`, `dsp.latency`, `dsp.registry`, `proc.*`, `ui.*` | the generic per-Mode rows, fidelity blocking |

Probe changes by M5 (out of the card's OWNS, for the lead's approval):
- `dsp.static`: a feedback configuration's slowest release skips the AUTO positions (A2's 50 ms fast path releases
  between a sine's peaks, and a loop that senses its output re-attacks only near them: 2 dB under the curve at 6:1);
  feed-forward AUTO positions (Bus G) keep their configuration and goldens.
- `dsp.time`: where the Mode's internal SC shaping is not flat (SLOW), `reversals` is judged once per stimulus period.

## Known limits

- The compressor senses x − r1, not the shared output (ModeEngine's frozen FB step solves stage 1 before stage 2): while
  the limiter holds the element the compressor charges as if it were alone. The element's GR is exact whenever the
  limiter's root is below the compressor's; above it, the compressor's state is higher than the hardware's.
- An external key: the compressor runs feed-forward on the key (E §2.6), but SharedElementMax cannot see
  `ControlIo::keyExternal`, so the limiter keeps its loop and senses key − GR.
- A1 / A2 in FB solve DualRelease's maps absolute (X10 gives the carry to SmoothBranching only, lead revision 4): a slow
  path releasing toward a non-zero GR stalls up to ulp(r) / (2 c_s) short of it — for A2 (τRs 5 s) at 8–16 dB about
  0.11 dB at 48 kHz and 0.9 dB at 384 kHz; A1 0.05 / 0.37 dB. A release to 0 dB does not stall.
- The attack is not program-dependent in A1 / A2 (2254: "attack adjusted automatically"); the limiter's knee and ratio,
  the switch steps of THRESHOLD / LIMIT THRESHOLD / GAIN and LIMIT RELEASE are [U] (D §8.6).
- The colour is memoryless: the transformers' LF saturation and the bridge's frequency dependence are not modelled.
- `ctBudgetNsPerSample` (70) is DW's; `fcmp_bench` was not run (other agents were building).

## Revision history

| Revision | Sprint | Change |
|---|---|---|
| 1 | S10 (M5) | Full traits (01 §10.7), provisional cleared: SharedElementMax limiter, A1/A2 DualRelease in FB, SlowHp, DiodeBridge; knee derived from RATIO; ADR-63 attacks; m[] routing per switch (ADR-64). Not shipped (no `modes-ever.tsv` row), so no bump. |
