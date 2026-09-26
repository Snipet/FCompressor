# CPU budgets and the bench report (S13 H1b)

Every Mode's `ModeDescriptor::ctBudgetNsPerSample` (E §3.7), the rule `fcmp_bench` judges it by, and the S13 H1b bench
report (lead revision 5c–d): what the telemetry cost, what was cut, and the budgets set from measurement. The unit is
E §3.7's: ns of `EngineHost::process` per base-rate sample per channel, stereo, the Mode's defaults, the bench program
(a 110 Hz triangle at −12 dBFS with −6 dBFS noise bursts), best of the repetitions.

## The rule (`Tools/bench/Bench.cpp`)

| Quality | A row's budget |
|---|---|
| ECO, STD | `ctBudgetNsPerSample` (E §3.7's STD figure: host, 2× IIR oversampler, control path, colour) |
| HQ | `ctBudgetNsPerSample` + 30 ns (`kHqAllowanceNs`) |

- **The HQ rule** is an allowance, not a multiplier: HQ adds the two cascaded linear-phase FIR halfbands (up and down,
  both channels) and runs gain, colour and mix at twice STD's rate. That cost is the host's, not the Mode's: HQ − STD
  measured +17…+23 ns for every Mode in this report (S12: +15…+26 at load 5), so a Mode-independent allowance with
  margin fits it; a multiplier would hand Mu 67 +45 ns and Clean +20 for the same filters.
- **Detached and attached** rows are both judged: the editor's telemetry is part of the Mode's cost while it is open.
- **The budget rows** are the 48 kHz / 128 rows (E §3.7's reference): each must be ≤ 1.0× its budget. The 96 kHz / 64
  and 192 kHz / 32 rows are printed with their ratio for information (their per-block work is spread over fewer
  samples). Every row must stay ≤ 3× (C §5.8's loose ceiling).
- `fcmp_bench --budget` exits 1 when a budget row or the 3× ceiling fails; each Mode ends with
  `budget <key>: pass|OVER (worst budget row …)`. CTest's `bench.<key>` runs without `--budget` (report only).

## Budgets

| Mode | Budget | HQ budget | Set by |
|---|---|---|---|
| Clean | 40 | 70 | E §3.7 (unchanged) |
| Bus G | 40 | 70 | E §3.7 (unchanged) |
| FET 76 | 60 | 90 | M2 (unchanged) |
| Opto 2A | **60** (was 50) | 90 | S13 H1b: measured with the editor open (below) |
| Mu 67 | **90** (was 60) | 120 | S13 H1b: measured with the editor open (below) |
| Diode 609 | 70 | 100 | DW (unchanged; measured 0.57× at STD) |
| Bus 25 | **40** (was DW's 45) | 70 | S13 H1b: measured (lead revision 5c) |
| Brickwall | **40** (was DW's 50) | 70 | S13 H1b: measured (lead revision 5c) |

## Report: before / after (48 kHz / 128, ns/sample/ch, detached / attached)

A/B in one run (lead revision 5): the same harness linked against the base engine (A, `497ff0d`) and this card's (B),
interleaved per Mode and Quality under `lockf -k -t 1800 /tmp/fcmp-bench.lock`, 4 s of audio, best of 7, the median of
three such runs; `agent` preset (RelWithDebInfo, -O3), machine load 3–5 (H1a building). Ratios are B against the new
budgets.

| Mode | ECO A → B | STD A → B | HQ A → B | B / budget (worst) |
|---|---|---|---|---|
| Clean | 16.6 / 21.0 → 16.4 / 19.5 | 22.3 / 27.3 → 22.2 / 25.5 | 39.5 / 45.3 → 39.5 / 42.9 | 0.64× (STD att.) |
| Bus G | 15.7 / 19.9 → 15.6 / 18.5 | 22.1 / 26.8 → 22.0 / 25.0 | 40.3 / 45.8 → 40.2 / 43.3 | 0.63× |
| FET 76 | 28.5 / 32.3 → 28.5 / 31.1 | 37.1 / 41.2 → 36.8 / 39.6 | 59.6 / 64.5 → 59.7 / 62.2 | 0.69× |
| Opto 2A | 44.0 / 96.9 → 31.8 / 46.9 | 46.1 / 105.2 → 40.0 / 55.0 | 74.0 / 127.7 → 61.4 / 77.0 | 0.92× |
| Mu 67 | 49.7 / 74.3 → 49.7 / 73.5 | 57.9 / 82.8 → 57.8 / 81.9 | 79.8 / 105.8 → 79.4 / 103.8 | 0.91× |
| Diode 609 | 28.9 / 32.6 → 28.8 / 31.1 | 37.3 / 41.7 → 37.2 / 39.6 | 59.4 / 64.4 → 59.3 / 61.9 | 0.62× |
| Bus 25 | 15.4 / 19.3 → 15.4 / 18.2 | 21.1 / 25.7 → 21.1 / 24.0 | 38.5 / 43.9 → 38.2 / 41.2 | 0.60× |
| Brickwall | 23.0 / 27.7 → 23.2 / 26.5 | 28.8 / 34.0 → 28.8 / 32.4 | 46.0 / 52.0 → 45.9 / 49.7 | 0.81× |

`fcmp_bench --budget` over every Mode and the whole grid after the change: exit 0, every Mode `pass` (worst: Mu 67
0.94×, Opto 2A 0.91×, Brickwall 0.88× at STD attached; that run's load 3.2). The S12 lead bench (load ≈ 5, `lead`
preset) read the same order: Bus 25 27.8 / 31.9 and Brickwall 27.0 / 31.8 at STD, before this card's cuts, so both new
budgets keep ≥ 12 % margin in every run so far. At 96 kHz / 64 and 192 kHz / 32 (information rows, one noisy run each)
every attach cost fell the same
way (Opto 2A +73 → +16 ns at STD 96 kHz, +59 → +16 at 192 kHz); Mu 67's 192 kHz STD attached row read 91 ns (1.0×).

## What the editor costs, and what was cut (lead revision 5d)

The common attach cost (every Mode) was +4…+6 ns/sample/ch; it is now +2.3…+3.8:

- `TelemetryAccum::accumulate` (the meters and the 1 ms columns, ~3.3 ns of Clean's 5): the loop runs on local copies
  of its accumulators and takes the last sample's control-path values once, after the loop. The operations and their
  order are unchanged, so the telemetry is the same bits.
- The colour-input meter (~0.7 ns): the meter reads only the block's peak, so the host hands it each chunk's peak over
  the OS samples, 4 wide, instead of a per-sample max.
- What remains is the telemetry contract itself (01 §6): the per-sample detector, target and stage-2 outputs, the
  columns, the UiFrame, and the Mode's internals latched once per 64 samples.

**Opto 2A** (+53…+59 → +15 ns): the FB kernel computes the static target `tgtDb` (the FB curve at the detector level x)
at every sample for the telemetry, and for Opto's curve that is `FeedbackZdf`'s Newton solve: most of the attach cost.
Two bit-exact cuts:

- `FeedbackZdf::solveFb` stops once every lane sits on a Newton fixed point (the remaining steps provably return the
  same bits). This also cuts Opto's detached cost by 6–12 ns: its delayed loop falls back to the same solve.
- `ModeEngine` keeps the last static target with the x and level controls it was solved for, and reuses it while both
  are the same bits (OptoSense holds its level 5 ms after a crest; the controls are constant once settled). This is an
  additive private member of a frozen class (for the lead's approval, S13 H1b handoff).

**Mu 67** (+24 ns, unchanged; why not): its static target is ProgressiveKnee's per-lane Newton at every sample, and its
PeakLog level moves every sample, so the memo never hits; its internals (latched every 64 samples and at every block)
include EFF RATIO, a 32-step bisection (~3 ns/sample/ch). Cutting either exactly needs what the frozen interfaces do not
offer: the target once per 1 ms column at the column's largest x (the FB curve is monotone in x; a `ControlIo` request),
or the history internal alone (an `IEngine` request). A target sampled on a grid is not exact: a periodic signal's
detector peaks fall on the same sample residues every period, and `ui.truth.diode-609` read 0.17 dB low (spec 0.1) at
every grid stride from 4 to 16 samples. So Mu 67's budget covers its measured cost with the editor open.

## Rerun

```sh
cd "$WT" && cmake --build --preset agent-probes
lockf -k -t 1800 /tmp/fcmp-bench.lock build-agent/fcmp_bench --budget                    # the whole grid, every Mode
lockf -k -t 1800 /tmp/fcmp-bench.lock build-agent/fcmp_bench --mode opto-2a --quality std --rate 48000
ctest --test-dir build-lead -L bench                                                        # the lead's quiet run
```
