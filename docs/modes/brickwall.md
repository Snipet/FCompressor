# Brickwall (slot 7, `brickwall`)

Mode sheet (01 §8.4 step 6): what Brickwall runs, every heuristic constant with its source and reason, and the revision
history. Owned by the Mode's card (M7 in S11). The contracts are 01 §10.7 (the sketch), 01 §5.2–5.4 (policies, engine,
the host's lookahead and side-chain delay), E §5.4 (the lookahead limiter) and K2 #21 (Brickwall correctness); the code is
`Source/fcdsp/modes/brickwall/` and the policy headers `stages/ballistics/SlidingMaxBox.h`,
`stages/detector/TruePeak4x.h` and `stages/colour/LoudClip.h`. The unit is D §2.8's generic look-ahead brickwall
limiter (M30): ratio ∞, attack realised inside the look-ahead window, release 1 ms–1 s with auto, true-peak detection
with 4× oversampling, a ceiling.

## Identity

| Field | Value |
|---|---|
| Key, slot | `brickwall`, 7 (`Modes.def`) |
| Group, rigor, family | limit, `Rigor::character` (ADR-65: a ceiling-referenced output is never unity below threshold), `CurveFamily::textbook` (∞:1 over a quadratic knee) |
| Topology | feed-forward only (`kTopologies = FF`) |
| Detector law | `peak` (DETECT PEAK) or `truePeak` (DETECT TP) |
| Stage 2 | none |
| Lookahead | `wantsLookahead = true`: LOOKAHEAD 0.5–20 ms inside the budget; locked 0 with the budget OFF (zero latency, overshoot allowed: the footer hint, 02 §6.6) |
| Revision | 1 |
| Provisional | no (cleared by M7, S11: its fidelity rows are blocking spec rows and its goldens can be blessed) |

## Traits (Brickwall.h)

| Slot | Policy | Parameters |
|---|---|---|
| Detector | `DetSelect<PeakLog, TruePeak4x>` | DETECT PEAK / TP (`kEngTruePeak`) |
| Computer | `QuadKnee` | THRESHOLD −30…0 dB, RATIO locked ∞:1, KNEE 0–6 dB |
| Link | `LinkMax` | LINK 0–100 % (on the targets, before the ballistics, E §8) |
| Ballistics | `CrestAuto<SlidingMaxBox>` | ATTACK derived = LOOKAHEAD, RELEASE 1–1000 ms, TIME MODE MAN / AUTO (CrestAuto, `kEngAutoRelease`) |
| Stage 2 | `NoStage2` | – |
| Colour | `ColourSelect<ColourNone, LoudClip>` | VOICE CLEAN / LOUD |
| SC shape | `Flat` | – |

`physical()`: `makeupDb = CEILING − THRESHOLD` (+ `LoudClip::kHeadroomDb` in LOUD), so the threshold lands on the
ceiling; `kEngAutoMakeup` is never set (K1 #27). STEREO MODE ST / MID / SIDE are the universal codes 0 / 2 / 3.
`EngineParams::m` is unused. `scDelaySamples(p)` (the Traits hook) is `TruePeak4x::kDelay` = 12 at DETECT TP, else 0.

## The lookahead limiter (SlidingMaxBox)

Per lane, with t the linked target GR (QuadKnee at ∞:1), L the lookahead and R the attack ramp in samples
(`round(ms · fs / 1000)`, exactly as EngineHost rounds `look`):

    h[n] = max(t[n − Wmax + 1 … n])      Wmax = max(R, L) + 2          (sliding max)
    b[n] = mean(h over L1)               L1 = ⌊(R + 2) / 2⌋             (box 1)
    a[n] = mean(b over L2)               L2 = R + 1 − L1 (≥ 1)          (box 2: L1 + L2 − 1 = R)
    r[n] = a[n] if a[n] ≥ r[n−1], else the release one-pole toward a[n]

- **R = L in the product.** ATTACK is derived from LOOKAHEAD (01 §10.7), so the resolver gives `atkTauMs = lookMs`. The
  engine still honours `atkTauMs` as the ramp (a slower attack than the lookahead completes late, as a compressor's
  would): `dsp.zipper` holds every detent render's attack at ≥ 50 ms, and an instantaneous ramp there read a waveform-
  shaped GR step on a kernel swap as a click (HQ stmode SIDE→MID, +18 dB).
- **Alignment (the `+2`).** The host delays the audio by L_la and the side chain by L_la − look + D_up − D_tp (01 §5.4
  c), so a target computed at engine time n belongs to the audio the gain meets at n + L. The boxes reach it at
  n + L − 1 and the sliding max holds it through n + L + 1: one base sample either side. That covers the OS-rate gain
  interpolation between two base samples and the up stage's fractional alignment (0 / 0.16 / 0.25 base samples at
  ECO / STD / HQ), so every point of a true-peak report [m, m + ¾] meets a GR at least its own target
  (`slidingmax.cover.violations` 0).
- **Two cascaded boxes [design choice, E §5.4's option].** The same attack length as one box of R, but an S-curve with a
  continuous slope: one 5 ms box's linear ramp has a slope step at each end, which the C §5.0 click metric reads (a
  switch into Brickwall while the carried GR ramps to the limiter's own: `dsp.switch` A→B→A +8.4 dB; the S-curve reads
  under the controls). First increment 10 dB / (L1 L2) instead of 10 dB / R (`slidingmax.ramp.first_step_ratio`).
- **Sliding max**: the two-stack queue in place on one ring per lane (amortised O(1), exact: `slidingmax.max.mismatches`
  0 over 60,000 samples while `look` slews). A snap (state recall) that lengthens the window re-folds the ring from the
  new start: it may hold high for one window, never low (`slidingmax.jump.*`).
- **Box sums (K2 #21c)**: a running sum per box and lane in double, re-summed exactly from its ring (oldest first) every
  **4096** samples [K2 #21c]. `slidingmax.resum.*`: with runs of 60 dB and 1e-20 dB targets the running sums keep a
  9.4e-21 dB residual after the window clears; the re-sum makes both boxes exactly 0.0.
- **`look` automation (K2 #21d)**: L and R move by at most one sample per control tick (`design()`), at the same
  absolute ticks where the host's SC delay line moves its read position (host/Delay.h), so the ramp follows the side
  chain; the box lengths change only at ticks, a one-sample change adjusts the running sum by the element that joins or
  leaves, a larger jump re-sums.
- **Release**: SmoothBranching's time smoothing and `advance()` (sub-ulp carry; `dsp.srsweep`'s long release 1e-3 dB),
  landing exactly on its target once within **kLandDb = 1e-6 dB** [H]: a one-pole toward 0 dB otherwise stalls where
  its step flushes to zero (≈ FLT_MIN / (1 − α): a GR of ~1e-35 dB that never becomes 0.0; the soak row needs 0.0).
- **Seed (01 §5.5)**: the applied GR continues at the carried value and the rings are filled with it, so the incoming
  engine holds at least that GR for one window. It cannot see peaks already inside the outgoing engine's window (a
  Carry holds one GR, not a history); the 20 ms crossfade's weight covers them.
- **Storage**: `PrepareInfo::scratch` (4 × nextPow2(L_la,max + 64) floats per slot): rings G0, G1 (P floats) and the
  box histories (P/2 each), exactly 4P. L and R are clamped to P − 4 (at 48 kHz P = 1024: 21.2 ms, above the 20 ms
  maximum). Only lanes 0–1 are computed; lanes 2–3 of every output copy them.

## True peak (TruePeak4x)

A 4× polyphase interpolator on the side chain only (latency unchanged). Each report covers the base sample m and the
three points m + ¼, ½, ¾ (phase 0 is the sample itself, so TP ≥ sample peak and a smooth signal reads as PeakLog does).

| Constant | Value | Source / reason |
|---|---|---|
| taps per phase | 24 (x[m − 11] … x[m + 12]) | [H] fitted: the Kaiser optimum for |H − e^{jωf}| up to 20 kHz at 48 kHz (0.4167 fs) is 0.31 dB with 12 taps (BS.1770's length), 0.10 dB with 16, 0.035 dB with 20, 0.012 dB with 24 — anything above 0.02 dB spends most of the HQ ceiling's +0.1 dB |
| window | Kaiser, β = 6.2, each phase at unit DC gain | [H] β fitted for the band to 0.4167 fs: 0.0079 dB-equivalent to 0.40 fs, 0.0124 dB at 0.4167 fs; 0.15 dB at 0.43 fs, 0.97 dB at 0.45 fs |
| D_tp | 12 base samples (0.25 ms at 48 kHz) | the interpolator's half length; `scDelaySamples()` = 12 at DETECT TP (K2 #21a) |
| TP OVER holds | 1 s release (`kHoldMs`) [H] | a meter-like readout |

BS.1770's own table is not used: its phases sit at ⅛, ⅜, ⅝, ⅞, so no phase contains the sample and its TP could read
below the sample peak. Measured against analytic sines (`truepeak.accuracy.*`): ≤ 0.0079 dB from 0.05 to 0.4167 fs.

**Budget**: TP aligns only while L_la − look + D_up ≥ D_tp. With a 5 ms budget and LOOKAHEAD at its 5 ms top, the host
floors the SC delay at 0 and the attack comes D_tp − D_up samples late (12 at ECO, 10 at STD; HQ's D_up = 33 covers it):
"TP without a budget cannot align" (01 §10.7) includes the top 0.25 ms of a 5 ms budget at ECO. The footer hint is keyed
on the budget OFF only (02 §6.6).

## VOICE LOUD (LoudClip)

The cubic soft clipper f(u) = u − 4u³/27 (|u| ≤ 3/2, ±1 beyond) of Adaa.h, level-compensated (unity small-signal gain,
ADAA-1 on the residual), on the wet signal after the gain element and before the ceiling makeup, at the OS rate. Its
GR is VOICE CLEAN's bit for bit (`bw.loud.gr_mismatches` 0, `dsp.static` `gr_matches_off`).

| Constant | Value | Source / reason |
|---|---|---|
| kSat | 3/2 | the shaper's corner (f = 1, f′ = 0) sits on the threshold: a held peak is rounded to T − 3.52 dB |
| kHeadroomDb | 20 log10(4/3) − 0.25 = 2.249 dB | [H] the makeup LOUD adds. 20 log10(4/3): a tone whose harmonics the band limit removes keeps its fundamental N(A)·A = A − A³/9 = 1.125 at A = 3/2, 1.02 dB over the clipped peak; 20 log10(1.5/1.125) puts that fundamental exactly on the ceiling, so single tones stay under it at every frequency. −0.25 dB: fitted margin for broadband program, where the cubic's in-band intermodulation adds to that bound (the stress program read +0.21 dB at HQ without it, −0.04 dB with it) |
| safety clip | ±1/k after the shaper | ADAA-1 on the residual returns the mean of f plus half the step, so a fast signal driven into the corner overshoots 1/k (several dB near Nyquist at base rate); the hard clip bounds every OS-rate sample (ECO: every output sample) at ceiling − 1.27 dB |
| clip-level smoother | 20 ms per tick, lands within 1e-4 dB | a voice's drive smoother (TubeSym.h), so threshold automation moves the corner with the gain computer's smoothed T |

LOUD is 0.67 dB louder in RMS than CLEAN on the music program at HQ (+2.25 dB for material under the threshold). Its
true peak after band-limiting: HQ ≤ −0.04 dB (stress) / −0.21 dB (music) over the ceiling; ECO +1.6…+1.9 dB (the
clipper's harmonics alias at base rate and reconstruct over the bound), STD +0.54…+2.1 dB (the same, plus the IIR round
trip): the ceiling specs are VOICE CLEAN's; LOUD's are the HQ row and the ECO sample bound.

## The ceiling (K2 #21b; `dsp.slidingmax`)

THRESHOLD −18, CEILING −1, LOOKAHEAD 5 ms in a 20 ms budget, DETECT TP, two programs 18 dB over the threshold,
band-limited to 20 kHz (a 511-tap linear-phase low-pass): the stress program (band-limited white-noise bursts, four
sines to 15 kHz, 12 kHz at 45° — samples 3.01 dB under the peak — 1 and 3 kHz squares, instant-onset noise hits, a sweep)
and the music program (pink noise, drums with 1 ms attacks, a piano-like chord, bass and hats, a sweep). True peak on a
reference 4× reconstruction (64 taps per phase):

| Quality | CLEAN, stress | CLEAN, music | Spec |
|---|---|---|---|
| ECO | +0.0000 dB | +0.0004 dB | ≤ +0.1 |
| HQ | +0.0005 dB | +0.0006 dB | ≤ +0.1 (K2 #21b) |
| STD | +2.15 dB | +0.84 dB | music ≤ +1.0 (01 §10.7: documented, not clipped); both: ≤ +0.1 over the STD round trip of the ECO render (measured 0.0000) |

**STD** overshoots by the 2× IIR round trip's own phase dispersion: the side chain is detected before the up stage, so
the limiter holds the input's true peak (ECO and HQ show it to within 0.001 dB), and the IIR halfbands then reshape
harmonic-rich waveforms (a band-limited square's true peak rises by 2.2 dB through the transparent round trip alone).
The STD render equals the STD round trip of the ECO render to 1e-7 dB (`bw.std.clean.*.excess_db`), so nothing of it is
the limiter's. Detecting on a model of the round trip would need a Quality-dependent SC delay, which the Traits hook
`scDelaySamples(EngineParams)` cannot see (the Quality is not in EngineParams).

DETECT PEAK at ECO: output sample peaks at the ceiling within 1e-6 dB; its true peak is not held (+0.3…+0.8 dB on
music). With the budget OFF (zero latency, R = L = 0, an instantaneous attack): +7.7 dB (ECO), +8.5 dB (STD), +0.65 dB
(HQ, whose up-stage delay gives the side chain 33 samples of lead) on the music program — the footer's "overshoot
possible".

**`look` automation** (HQ, music, 1 → 10 → 2 ms in 16-sample blocks): +0.097 dB (spec ≤ +0.25): while `look` moves the
host skips or repeats one SC sample per tick (K2 #21d), which the true-peak interpolator sees as a discontinuity; LOOK EFF
moves at most one sample per tick, lands, and the latency never changes.

## Internals (UiFrame)

| Word | Name | Reads |
|---|---|---|
| 0 | HELD PEAK (dB, history) | the input peak level the limiter holds its applied GR for: the ∞:1 curve inverted at the applied GR r of the louder lane (T + r above the knee, T + √(2Wr) − W/2 inside it; the smoothed T and W), which the release keeps smooth between waveform peaks; −60 dB (the readout's floor) while no GR is applied. A 1 kHz square at T + 6 reads T + 6. (The sliding max itself follows the waveform when the window is short, budget OFF: `ui.truth`'s history-lane row read 1.96 px of per-sample jitter on it, so the readout uses the applied GR.) |
| 1 | LOOK EFF (ms) | the lookahead the window runs now (L, slewing one sample per tick) |
| 2 | TP OVER (dB) | the held true peak over the held sample peak (1 s holds), max of lanes 0–1; 0 at DETECT PEAK. 12 kHz at 45° reads 3.01. |

## Verification

| Probe | What it holds Brickwall to |
|---|---|
| `dsp.slidingmax` | SlidingMaxBox (brute-force max and boxes, coverage, ramp, hold, the exact re-sum, `look` slew and jumps, seed, no scratch, NaN, release), TruePeak4x (coefficients, accuracy against analytic sines, phase 0, delay, seed, TP OVER, the engine's `scDelaySamples`), LoudClip (bound, unity, corner, transfer, silence, block splits, level smoothing, the headroom constant), and the Mode through EngineHost: the ceiling at ECO / STD / HQ, DETECT PEAK, LOUD, `look` automation, internals, a 60 s TP soak |
| `dsp.null.brickwall` | the generic nulls plus the 10-minute below-threshold soak (`null.soak.*`: STD, 5 ms, after 2 s of 12 dB over: GR exactly 0.0 from 32 s on) |
| `dsp.static`, `dsp.time`, `dsp.srsweep`, `dsp.link`, `dsp.analysis`, `dsp.hostile`, `dsp.switch`, `dsp.zipper`, `dsp.quant`, `dsp.rt`, `dsp.print`, `dsp.latency`, `dsp.registry`, `proc.*`, `ui.*` | the generic per-Mode rows, fidelity blocking |

Probe changes by M7 (out of the card's OWNS, for the lead's approval):
- `dsp.null`: the soak rows (03 §3.4 names them; they did not exist), keyed on `desc.wantsLookahead`.
- `dsp.srsweep`: an attack published below one sample at 48 kHz (Brickwall with the budget OFF: instantaneous) is judged
  by the 1.5-sample tau floor (`atk.instant_diff_s`) instead of the ratio of two one-sample times.
- `dsp.analysis`: a true-peak configuration's settled row is a NOTE (the TP of a square reads the band-limited overshoot
  at its edges, +2 dB, so the plateau's tapped level cannot predict the held GR).

## Known limits

- STD's true peak is the IIR round trip's (above); content between 20 kHz and Nyquist at 48 kHz (above 18.4 kHz at
  44.1 kHz) is under-read by the 24-tap interpolator (0.15 dB at 0.43 fs, 1 dB at 0.45 fs).
- TP at the top of the budget cannot align (the SC delay floors at 0): up to D_tp − D_up late.
- A Mode switch into Brickwall cannot see peaks already inside the outgoing engine's window.
- LOUD's ceiling is the HQ row; at ECO and STD the clipped waveform reconstructs over it (above).
- `ctBudgetNsPerSample` (50) is DW's; `fcmp_bench` was not run (other agents were building). TP adds 72 fma per
  sample (4 lanes), the sliding max and two box sums are O(1) amortised per lane.

## Revision history

| Revision | Sprint | Change |
|---|---|---|
| 1 | S11 (M7) | Final traits (01 §10.7), provisional cleared: SlidingMaxBox (two cascaded boxes, exact re-sum, tick-aligned `look`), TruePeak4x (24-tap 4× SC, D_tp 12), LoudClip (corner on the threshold, 2.25 dB headroom, safety clip). Not shipped (no `modes-ever.tsv` row), so no bump. |
