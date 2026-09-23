# E: Compressor DSP engine (FCompressor)

Scope: the DSP engine that hosts every Mode, plus the parameter, latency and telemetry contracts where they meet the
engine. This report complements the other research reports and does not repeat them:
- A covers the GUI stack.
- B covers the plugin, build and parameter mechanics. Its §6.1 covers the GlueCompressor seed, §7.4 the parameter
  model and §7.5 telemetry.
- C covers verification. Its §5.1 is the Mode contract and §5.2–5.7 are the probes.
- D is the Mode catalogue. Its §4 is the universal parameter superset, §5.7 the descriptor and §6.2 the engine
  features F1–F17.

§11 lists where this report agrees with those and where it changes them.

Tags:
- **[P]** published source (listed in §13).
- **[D]** derived here, with the math shown.
- **[M]** measured on this Mac in this session with scratch programs, not committed; the programs are in §12.
- **[H]** heuristic starting value that must be fitted or checked against the unit.

HardwareReverb (HR) line numbers were current at read time (2026-09-22 ~20:15). HR is being edited at the same time
(for example, `renderPulled` appeared while this report was written), so each citation also gives a symbol name.

---

## 0. Decisions on one screen

1. **One pipeline with fixed stage slots. A Mode is a traits struct** that picks one policy per slot:
   - SC route/filter
   - detector
   - gain computer
   - link
   - ballistics
   - topology
   - Stage 2
   - gain element/colour

   The traits also carry constants and a `ModeSpec` table (§4). Each Mode compiles to `ModeEngine<Traits>`, and the
   host calls it through **one virtual call per block, never per sample**. Engines are placed into two preallocated
   arena slots (A/B), so a Mode switch never touches the heap on the audio thread.
2. **Control path in the dB gain-reduction domain.** This is the Giannoulis/Massberg/Reiss placement: smoothing
   *after* the gain computer, so "return to zero" means "return to 0 dB GR". It uses SIMD across channels
   (`f32x4` lanes = {ch0, ch1, aux0, aux1}).

   [M] The core FF kernel (log2 → quadratic knee → smooth branching → exp2) runs at:

   | Implementation | ns per sample per channel |
   |---|---|
   | NEON, 2 lanes live | **1.95** |
   | NEON, 4 lanes live | 1.06 |
   | scalar libm | 9.29 |

   The recursion's latency chain, not arithmetic, is the bound. So pack independent recurrences (crest detectors,
   Stage 2) into lanes 2–3.
3. **Feedback Modes use a zero-delay-feedback (implicit) solve.** It has a closed form for the hard and quadratic
   knees, checked against bisection to 1.4e-14 dB over 200 000 random cases [M].

   [D] The naive one-sample-delay loop has attack-branch pole `p = α − (1−α)(R−1)`. For a 20 µs attack at 48 kHz it
   is **unstable above 3.09:1** and rings above 1.55:1.

   [M] A 4:1 simulation settles into a ±8 dB period-2 chatter instead of 15 dB of GR. At 192 kHz, 20:1 is still
   unstable. The ZDF form's pole is `α/(1+(1−α)S)`, which is always in (0, α). The FB **static curve is the same
   solver with α = 0**.
4. **Parameters: one fixed universal host set for all Modes** (D §4 IDs). Each Mode's `ParamSpec` filters their
   meaning.
   - The raw host value is **never rewritten by a Mode change**.
   - The resolver snaps on read. The UI snaps on write (step index → canonical value).
   - Host text comes from `stringFromValue` reading the active Mode.
   - The Mode parameter is `AudioParameterInt 0..127` with append-only slots and a stable string key in state.
5. **Latency depends only on global setup (Quality, Lookahead-enable), never on the Mode.**
   - A Mode switch is a **20 ms equal-gain crossfade** of two engines (HR's bypass ramp is 20 ms, PluginProcessor.cpp:83).
   - The new engine is **seeded with the old one's applied GR and detector level**, so the gain does not jump.
6. **Colour and aliasing.**
   - Default: JUCE 2× polyphase IIR at integer latency, **4 samples** [M].
   - "Eco": ADAA-1 on the *nonlinear residual* only, 0 latency.
   - "HQ": 4× FIR at max quality, **61 samples** [M].
   - Dry/wet mix happens **inside the oversampled domain**, so dry and wet pass through identical filters.
7. **Analysis shares code with the DSP.** The static curve is the Mode's own SIMD gain-computer/solver, called with
   four abscissae at a time. The step-response preview runs a private instance of the *same* `ModeEngine<Traits>` on a
   synthetic tone burst on the message thread. It costs about 1–3 ms per recompute [D from M].
8. **Telemetry** uses HR's seqlock-over-atomic-words `UiFrame` (FDNReverb.h:26-39, FDNReverb.cpp `publishUiFrame`
   :1274). The frame holds per-lane detector level, target GR, applied GR, `internals[16]`, and the snapped parameters
   in use. A separate SPSC **min/max column ring** at 1 kHz feeds the GR history.
9. **Own `log2`/`exp2` polynomials instead of libm in the hot path** [M]:
   - Error: 2.3e-5 dB for log2 and 7.4e-4 dB for exp2.
   - Written only with `fma()`, so NEON and x86 round identically. That keeps cross-arch goldens exact, which HR's
     Simd.h shows libm `powf` does not (Simd.h:49-52).

---

## 1. HardwareReverb DSP idioms FCompressor should inherit

| Idiom | Where in HR | Use in FCompressor |
|---|---|---|
| `f32x4` is a **type alias with thin forwarders**, not a wrapper class. It has NEON and SSE/FMA backends, `fma(a,b,c)=a+b*c` on both, and operand order handled for x86 | `Source/dsp/Simd.h:54-68, 75-86, 100-117` | `fcmp::simd` is a superset: min/max/abs/div/sqrt/floor/compare/select/log2/exp2 (§3.2). Keep the `#error` when FMA is absent (Simd.h:63-65). |
| Bit-exactness is a design goal. libm last-bit differences are documented | Simd.h:34-52, CMakeLists.txt:334-387 | No libm in the per-sample path, so arm64 and x86 goldens can match exactly. |
| DSP flags in one INTERFACE target: `-O3 -fno-math-errno -fno-trapping-math`, **no `-ffast-math`** | CMakeLists.txt:361-387 (`HardwareReverbDspFlags`) | Same target (`FCompressorDspFlags`) for the plugin and the Tools harnesses. |
| Parameters **polled** from APVTS raw atomics instead of listeners, because listener locks have no priority inheritance | PluginProcessor.h:107, 119 (`Raw`), PluginProcessor.cpp:262 (`pushParametersToEngine`) | Same. The resolver (§4) reads the raw atomics once per block. |
| Preset recall snap with release/acquire ordering (`requestParameterSnap`, `snapPending_`) | FDNReverb.cpp:291, :730; PluginProcessor.cpp ~411 (`pullParameters`) | Same for smoothers. A preset that changes Mode still crossfades (§5). |
| `smoothSnap`: one-pole plus epsilon landing, because a float one-pole stalls a few ulps short | FDNReverb.cpp:750 | Same landing rule. FCompressor smooths **per sample**, not per block (§4.6, C §5.6.3). |
| Block-size-compensated block-rate coefficient | FDNReverb.cpp:715 | Telemetry only. The audio path must be block-size invariant (C §5.6.3). |
| `UiFrame`: trivially copyable, whole words, published as `std::array<std::atomic<uint32_t>>` behind a seqlock, gated by `setUiAttached` | FDNReverb.h:26-44, 309-327; FDNReverb.cpp:1274 `publishUiFrame`, :1338 `readUiFrame` | Same pattern (§7). |
| NaN/Inf poison check at block rate, then `reset()` | FDNReverb.cpp:1263-1267 | Check engine state and output every block, then reset the engine. |
| `juce::ScopedNoDenormals` in `render`; `ScopedFtz` in harnesses | PluginProcessor.cpp:346; Tools/Harness.h:50 | Same, plus log-domain floors (§3.8). |
| Never resize on the audio thread; chunk long host blocks | PluginProcessor.cpp:442 (`maxChunk`) | Same, with an internal chunk of 64 samples (§3.4). |
| 20 ms bypass ramp, bit-exact at both ends | PluginProcessor.cpp:83, 306 | The same shape for bypass, Mode crossfade and SC listen. |

---

## 2. Literature, reduced to the equations we implement

### 2.1 Notation

| Symbol | Meaning |
|---|---|
| s[n] | side-chain sample |
| ℓ | level in dB |
| T | threshold, dB |
| W | knee width, dB (≥ 1e-4) |
| R | ratio |
| **S = 1 − 1/R** | the "slope": dB of GR per dB of overshoot. S = 0 is 1:1, S = 1 is ∞:1, S = 2 is −1:1 (D §0.2's `s`) |
| r ≥ 0 | applied gain reduction in dB (positive = attenuation) |
| r̂ | gain-computer target GR |
| M | makeup, dB |
| g | linear gain, `10^((M − r)/20)` |

Conversions:
- `ℓ = 20·log10|s| = 6.0205999·log2|s|`
- mean square: `ℓ = 10·log10(ms) = 3.0103·log2(ms)`
- `g = exp2((M − r)/6.0205999)`
- Floors: |s| ≥ 1e-12 (−240 dB) and ms ≥ 1e-24.

### 2.2 Gain computer (static curve)

**Quadratic soft knee** [P Giannoulis et al. 2012; MATLAB `compressor` doc gives the same formula]:

```
y_G = x_G                                      if 2(x_G − T) < −W
y_G = x_G + (1/R − 1)(x_G − T + W/2)² / (2W)   if 2|x_G − T| ≤ W
y_G = T + (x_G − T)/R                          if 2(x_G − T) > W
r̂  = x_G − y_G
```

**Branchless form** (verified equal) [D]:

```
o = x − T,   q = clamp(o + W/2, 0, W),   r̂ = S·( q²/(2W) + max(0, o − W/2) )
```

This is GlueCompressor's `curveGrDb` (B §6.1) with no branches. A hard knee is W → 1e-4.
- **Limiter:** S = 1.
- **"Ratio above infinity":** S > 1. dbx 160X runs 1:1 → ∞:1 → −1:1, "INFINITY+" [P dbx 160X manual/cut sheet], so
  S ∈ [0, 2].
- **Display:** `R = 1/(1 − S)`. It is continuous through ∞, and the sign flips beyond.
- **Negative ratios exist only in FF** (§2.6).

**Auto makeup:** `M = r̂(0 dBFS)` [P MATLAB: `M = −x_sc(0)`]. A hard knee gives `M = −S·T`. API's auto makeup "based
on ratio and threshold" [P API 2500+ manual] is a Mode policy, for example `k·r̂(0)` with k ≤ 1.

**Progressive (level-dependent) ratio** [D], for vari-mu, VSC-2 Soft, GML Soft and Manley Limit (D §3):

```
o > 0:  r̂(o) = S_max·( o − o_c·(1 − e^(−o/o_c)) )
        local slope s(o) = dr̂/do = S_max·(1 − e^(−o/o_c))
        local ratio R(o) = 1/(1 − s(o))
```

- It is C¹, soft by construction (s(0) = 0) and monotone, and costs one `exp2`.
- Example, S_max = 0.95 and o_c = 8 dB:

  | o (dB over threshold) | local ratio |
  |---|---|
  | 2 | 1.27:1 |
  | 4 | 1.6:1 |
  | 8 | 2.5:1 |
  | 16 | 5.6:1 |
  | 24 | 10.3:1 |

  That matches the Fairchild's "1:1–2:1 for smaller peaks … up to 20:1" [P SOS Fairchild review] with o_c ≈ 8–12 dB
  [H].
- **Local ratio of any curve:** `R_local(x) = 1 / (1 − dr̂/dx)`. The UI shows this at the operating point.

**Table curve**, for measured units: monotone PCHIP over a 1 dB grid, with Eichas's pre- and post-gains. g_pre shifts
the curve along the input axis and g_post scales the gain axis [P Eichas & Zölzer 2016, §5.1].

**SSL G bus:** its "soft knee where the knee width is automatically computed based on the threshold and ratio" [P
Solid State Bus-Comp dataset paper]. The law is not published, so it goes in the Mode's `resolve()` as `W = f(T, R)`
[H].

### 2.3 Time-constant conventions (Modes publish times under different definitions)

| Convention (C §5.1 `TimeLaw`) | Step response reaches | t / τ | Who uses it |
|---|---|---|---|
| τ, `expDb` / `expLin` | 63.2 % | 1 | Giannoulis: α = e^(−1/(τ·fs)) |
| 10→90 % rise, `t10_90` | 10 %→90 % | ln 9 = 2.197 | MATLAB (`αA = exp(−log(9)/(Fs·TA))`) [P], Zölzer's "2.2", the NR LA-2A paper's `α = 1 − exp(−2200·T/t_ms)` [P arXiv 2509.10706] |
| 0→90 % | 90 % | ln 10 = 2.303 | some datasheets |
| 99 % ("digital") | 99 % | ln 100 = 4.605 | Pirkle `AudioDetector` "digital" mode (analog mode ≈ 63 %) [P book; the exact constants were not re-verified] |
| 50 % | 50 % | ln 2 = 0.693 | LA-2A "60 ms for 50 % release" [P Teletronix manual via Vintage King/Wikipedia] |
| rate | dB per second | — | dbx 160 "125 dB/sec" [P] |

- A one-pole decaying in the amplitude domain releases at 8.686/τ dB/s. In the power domain the rate is 4.343/τ dB/s.
- **Every `ParamSpec` time carries its `TimeLaw`.** The resolver converts the published time to the internal τ before
  computing α = exp2(−1/(τ·fs·ln2)) with the poly `exp2`.
- Watch the sign convention. Some papers write `α·x + (1−α)·y[n−1]`, where their α is Giannoulis's (1 − α).

### 2.4 Level detectors and where they go [P Giannoulis et al. 2012]

Level detectors (x = detector input, y = output):

| Detector | Equation |
|---|---|
| One-pole | `y[n] = α·y[n−1] + (1−α)·x[n]` |
| Branching peak | `y[n] = α_A·y[n−1] + (1−α_A)·x[n]` if `x[n] > y[n−1]`, else `α_R·y[n−1]` |
| Smooth branching | as branching, but the release branch is `α_R·y[n−1] + (1−α_R)·x[n]` |
| Decoupled | `y1[n] = max(x[n], α_R·y1[n−1])`, then `y[n] = α_A·y[n−1] + (1−α_A)·y1[n]` |
| Smooth decoupled | `y1[n] = max(x[n], α_R·y1[n−1] + (1−α_R)·x[n])`, then `y[n] = α_A·y[n−1] + (1−α_A)·y1[n]` |
| RMS (mean square) | `y²[n] = α·y²[n−1] + (1−α)·x²[n]` |

Branching versus decoupled:
- Branching gives the release time as set.
- Decoupled gives lower ripple, but the attack filter lengthens the effective release.
- The smooth variants remove the discontinuity at the attack/release switch.

The paper considers three placements:
1. A linear-domain detector before log and the gain computer.
2. A log-domain detector before the gain computer.
3. A log-domain detector *after* the gain computer, on `x_L = x_G − y_G` (the GR amount).

Placements 1 and 2 are "return-to-zero" toward the wrong zero. In placement 2 the zero is 0 dBFS. In both, the
effective release depends on how far above threshold the signal was, because GR stops when the envelope crosses T.
In placement 3, zero *is* "no gain reduction". Attack and release then act on GR changes, independent of threshold and
level.

**FCompressor default: placement 3 with smooth branching.** This is also what CTAGDRC implements: "the smooth
branching peak detector … placed behind the gain computer" [P CTAGDRC README]. Smooth decoupled is a policy option.
Linear-domain and RMS detectors (placements 1 and 2) remain as *Mode* policies for analog-faithful units (§2.7).

Detector calibration for the curve's x-axis (C §5.2):
- For a sine, a peak detector reads the peak.
- An RMS detector reads peak − 3.0103 dB.
- `ModeSpec::detector` declares which, and the x-axis is "the level the gain computer sees" (§6.2).

### 2.5 Program-dependent and automatic time behaviour

**a) Crest-factor automation** [P Giannoulis, Massberg, Reiss, JAES 2013 "Parameter automation…"; CTAGDRC]:

```
C² = y_peak² / y_rms²          both detectors over ~200 ms
τ_A = 2·τ_A,max / C²
τ_R = 2·τ_R,max / C² − τ_A
```

Transient material (high C) gets faster times. The peak and RMS detectors are independent recurrences, so they fill
lanes {pkL, pkR, msL, msR} of one `f32x4` for free (§3.2).

**b) Dual release, SSL-style Auto** [P KVR thread]:
- The circuit has two RC release paths: a fast one near the fastest release setting (0.1 s) and a slow one near the
  slowest (1.2 s).
- "The smaller capacitor charges first … if the signal stays at high level long enough, then the larger capacitor will
  also charge."
- GlueCompressor already cascades 50 ms into 600 ms with `max()` (B §6.1).

Engine form, in the GR domain:

```
r_f ← smooth-branching(r̂; α_A, α_Rf)                  τ_Rf ≈ 0.1 s   [H]
r_s ← one-pole toward r_f; α_C if r_f > r_s, else α_Rs   τ_C ≈ 0.3 s, τ_Rs ≈ 1.2 s [H]
r   = max(r_f, r_s)
```

After a short transient r_s barely charges, so the release is fast. Sustained GR charges r_s, so the release is slow.

**c) Rate release, and why a log-domain RMS detector *is* the dbx 160** [D]. dbx 160 specifies:
- attack of 15 ms for 10 dB, 5 ms for 20 dB and 3 ms for 30 dB over threshold;
- release of 125 dB/s (8 ms for 1 dB, 80 ms for 10 dB, 400 ms for 50 dB) [P dbx 160/160A specs].

A mean-square one-pole (time constant τ) feeding a log gain computer gives:
- **Release:** `ms(t) = ms0·e^(−t/τ)`, so ℓ falls *linearly* at 4.343/τ dB/s. τ = 34.7 ms gives exactly 125 dB/s.
- **Attack:** a step to D dB over T, measured as time to 63 % of the final GR, is `t = −τ·ln(1 − 10^(−0.0368·D))`.

  | τ | D = 10 dB | D = 20 dB | D = 30 dB | Release |
  |---|---|---|---|---|
  | 34.7 ms | 19.4 ms | 7.0 ms | 2.8 ms | 125 dB/s |
  | 27 ms | 15.1 ms | 5.5 ms | 2.2 ms | 161 dB/s |
  | 30 ms, simulated with a 1 kHz sine [M] | 16.8 ms | 6.2 ms | 2.4 ms | — |

So the "program-dependent" dbx behaviour comes out of one `RmsLog` detector with τ ≈ 30 ms, with no special
ballistics. An explicit **rate** policy (`r = max(r̂, r − ρ/fs)`) covers dbx 165A-style dB/s controls (D §3).

**d) Multi-stage smoothing** [P Eichas & Zölzer 2016 generic model]:
- Three first-order low-passes with separate attack and release coefficients, mixed by weights β1, β2 ∈ [0,1] "so
  that no energy is lost or added". The exact wiring is in their Fig. 11 and was not text-extractable.
- Our policy: `r = w1·r1 + w2·r2 + (1−w1−w2)·r3`. Each r_i is smooth-branching with its own (α_A,i, α_R,i). The
  weights and coefficients are fitted offline (§2.8).
- Their detector also uses **different coefficients depending on the sign of the previous sample**, which produces
  odd harmonics. This is a cheap colour trick for opto and guitar-comp Modes.

**e) History-dependent release** (opto memory, Fairchild TC5/TC6): see §2.7.

### 2.6 Feedback topology: static curve, the discrete loop, and the zero-delay solve [D]

**Static curve.** In the FB gain computer the GR slope `k` applies to *output* overshoot, `r = k·(y − T)` with
`y = x − r`. That gives

```
y − T = (x − T)/(1 + k)    so    R_eff = 1 + k,   and   k = R − 1 = S/(1 − S)
```

This matches D §5.7. Consequences:
- ∞:1 needs k → ∞, so the engine clamps k ≤ 99 (100:1).
- **Negative ratios are impossible in FB**, because the slope is always positive. Negative-ratio Modes must be FF.
- In FB the knee is defined at the output. The input-domain knee spans `[T − W/2, T + (1+k)·W/2]`: asymmetric and
  wider. The UI must use the solver (below), not the FF formula (D §0.5).

**The naive one-sample-delay loop is unstable at FET speeds.** Take the attack branch, a hard knee in the log domain,
and the detector reading y[n−1]:

```
r[n] = α·r[n−1] + (1−α)·k·(x − r[n−1] − T)
pole p = α − (1−α)·k
stable iff k < (1+α)/(1−α);  non-ringing (p ≥ 0) iff k ≤ α/(1−α)
```

Largest ratio R_eff = 1 + k:

| attack τ | fs | α | stable below | non-ringing up to |
|---|---|---|---|---|
| 20 µs | 48 k | 0.3529 | **3.09:1** | 1.55:1 |
| 20 µs | 96 k | 0.5940 | 4.93:1 | 2.46:1 |
| 20 µs | 192 k | 0.7707 | 8.72:1 | 4.36:1 |
| 50 µs | 48 k | 0.6592 | 5.87:1 | 2.93:1 |
| 100 µs | 48 k | 0.8119 | 10.6:1 | 5.3:1 |
| 300 µs | 48 k | 0.9329 | 29.8:1 | 14.9:1 |
| 800 µs | 48 k | 0.9743 | 77.8:1 | 38.9:1 |
| 3 ms | 48 k | 0.9931 | 289:1 | 144:1 |

[M] Simulation, 1176 fastest attack, 20 dB over threshold:
- **4:1 at 48 k:** GR alternates 24.88 / 8.78 dB forever instead of settling at 15 dB, i.e. a Nyquist-rate buzz.
- **20:1 at 192 k:** still diverging (30.5, 23.5, 18.1, 22.1 …).

Oversampling alone does not rescue the 1176. An opto loop (τ ≈ 10 ms) is safe with the naive loop at any ratio,
which is fine because opto is not a precision device.

**Zero-delay solve.** Solve `r = α·r1 + (1−α)·r̂_fb(x − r)` for r. Here x is the detector-domain level of the *input*
side-chain, and the loop senses `y = x − r`. Define `F(r) = r − α·r1 − (1−α)·r̂_fb(x − r)`. Then
`F′(r) = 1 + (1−α)·r̂_fb′ ≥ 1`, so there is exactly one root and Newton iteration is monotone. Closed forms:

```
1) linear region:  r = [α·r1 + (1−α)·k·(x − T)] / [1 + (1−α)·k]              valid if x − r − T ≥ W/2
2) knee region:    b = x − T + W/2,  c = b − α·r1,  κ = (1−α)·k/(2W)
                   u = 2c / (1 + sqrt(1 + 4κc))   (stable root form)
                   r = b − u                                                    valid if c > 0 and u ≤ W
3) below knee:     r = α·r1
branch α: α_A if r̂_fb(x − r1) > r1 else α_R    (explicit predictor)
```

- [M] Checked against 200-step bisection over 2·10⁵ random (x, r1, α, T, W, k): max |Δ| = 1.4e-14 dB.
- **Effective pole** `α/(1 + (1−α)·k)` ∈ (0, α): always stable and never rings.
- **The static FB curve is the same function with α = 0.** One code path serves the DSP and the characteristics
  screen.
- Other gain laws, such as the FET voltage-controlled resistor, vari-mu bias → gm, or a linear-domain rectifier in
  the loop: use **two fixed Newton iterations** from the predictor. Two is enough because convergence is quadratic,
  and a fixed count keeps it deterministic.
- For a linear-domain detector state e: `F(e) = e − α·e1 − (1−α)·|s_x|·G(e)`, where G is the element's linear gain as
  a function of detector state. G is non-increasing, so F′ ≥ 1 again.

**Side-chain filters inside an FB loop.** Use `filter(x·g) ≈ g·filter(x)` for a slowly varying g. That means:
filter the *input* side chain, take its level, and subtract r in the log domain. This is exact for static gain and
first-order accurate otherwise, and it keeps the loop scalar.

**External key in an FB Mode.** The key replaces the sensed signal, so the Mode evaluates FF on the key. Hardware FB
units with a key input behave this way.

### 2.7 Hardware families: DSP models and the numbers that parameterise them

D §2 has the full unit catalogue. This section records only what the engine needs.

| Family | Loop / detector | Published numbers | Engine model |
|---|---|---|---|
| **FET (1176)** | FB; peak | attack 20–800 µs, release 50–1100 ms, ratios 4/8/12/20 plus all-buttons, fixed threshold (Input drives it); higher ratios raise the threshold; "soft knee … ratio increases slightly after transients" [P Wikipedia] | `FeedbackZdf` + FET gain law + `FetColor`. Grey-box fitting follows Gerat, Eichas & Zölzer (AES 143, 2017): a block-oriented model tuned by Levenberg–Marquardt from I/O measurements only [P]. All-buttons is a ratio `Step` with `tag = ALL` that switches to a custom curve (a negative-slope region), colour drive up and slower attack [H]. |
| **Opto (LA-2A / T4B)** | FB; EL panel + LDR | attack "average 10 ms"; release "60 ms for 50 % release, then … 1 to 15 seconds", program-dependent [P Teletronix manual via Vintage King]. VTL5C2 LDR (a different cell, same physics): turn-on below 1 kΩ within 5 ms; turn-off above 1 MΩ after 500 ms, "100 times longer" [P Eichas & Zölzer 2016]. VTL5C3/2 heuristic: 12 ms up, 250 ms down, rate further modulated by current value; `R = A/I^1.4 + B`, A = 3.464 Ω·A^1.4, B = 1136.212 Ω [P Parker & D'Angelo 2013] | `OptoCell` (below). NR-fitted feed-forward equivalent: threshold about −36 dB, ratio about 4, attack about 10 ms, release 60–1500 ms depending on Peak Reduction [P arXiv 2509.10706]. This is a good cross-check target. |
| **Vari-mu (Fairchild 670)** | FB; 6386 remote-cutoff | TC1 0.2 ms / 0.3 s; TC2 0.2 ms / 0.8 s; TC3 0.4 ms / 2 s; TC4 0.8 ms / 5 s; TC5 0.4 ms / 2 s (peaks), 10 s (multiple peaks); TC6 0.2 ms / 0.3 s, 10 s, 25 s (programme) [P SOS]. Ratio from ~1–2:1 up to ~20:1 [P SOS]. The TC4 attack is disputed (D §8.2) | `ProgressiveKnee` inside `FeedbackZdf`. TC5 and TC6 use `MultiStage` with history-dependent weights [H]. `VarimuColor`: odd-dominant, since push-pull cancels even harmonics, and it grows with GR [H]. |
| **VCA bus (SSL G)** | FF (VCA) | ratio 2/4/10; attack 0.1/0.3/1/3/10/30 ms; release 0.1/0.3/0.6/1.2 s/Auto [P SOS, Waves manual]; auto knee from T and R [P SSB paper] | `PeakLog` → `QuadKnee` (W from `resolve`) → `DualRelease` when Auto. |
| **VCA (API 2500+)** | NEW = FF, OLD = FB; RMS detector | ratio 1.5, 2, 3, 4, 6, 10, ∞; attack .03, .1, .3, 1, 3, 10, 30 ms; release .05, .1, .2, .5, 1, 2 s, or variable 50 ms–3 s; knee Hard/Med/Soft; Thrust is "a filter in front of the RMS detector, with a slope of 10 dB per decade" (= 3.01 dB/oct, inverse pink), MED/LOUD; link IND or 50–100 % in six steps, with FAST (peak) / SLOW (RMS) / BOTH link shape; each channel keeps its own detector and the control voltages are summed; auto makeup from ratio and threshold; threshold +10 to −20 dBu [P API 2500+ manual]. SOS's "Med 2 dB/oct, Loud 4 dB/oct" conflicts with this (D §8.5) | `RmsLog` + tilt SC (§8) + `QuadKnee` with 3 W values. `topo` switches FF↔FB as a kernel switch through the crossfade (§5.1). |
| **VCA RMS (dbx 160/160X)** | FF, true RMS | as §2.5c; ratio 1:1 → ∞ → −1:1 [P] | `RmsLog(τ ≈ 30 ms)` + `QuadKnee` (OverEasy = wide W), S ∈ [0, 2]. |

**`OptoCell` model** [H: structure from the sources above, constants to be fitted].
- Lanes are channels. The state is LDR conductance in normalised units, split into fast and slow parts, plus a memory
  term.
- e is the side-chain drive in the FB loop: the rectified, HF-emphasised output (R37 trim = emphasis shelf).

```
light  L  = max(0, e − e0)^γ                         (EL panel, γ ≈ 1–1.5)
target G∞ = L
fast:  G_f += (G∞·w > G_f ? a_on : a_off,f)·(G∞·w − G_f)          τ_on ≈ 10 ms, τ_off,f = 60 ms/ln2 = 87 ms
slow:  G_s += (G∞·(1−w) > G_s ? a_on : a_off,s(m))·(G∞·(1−w) − G_s)
       τ_off,s(m) = 0.3 s + 3.2 s·m                  (full release 1–15 s ≈ 5·τ)
memory m += (G_f+G_s > θ ? a_m,up : a_m,dn)·(1{G>θ} − m)          τ_m,up ≈ 5 s, τ_m,dn ≈ 20 s
gain   g  = 1 / (1 + R_s·(G_f + G_s))                (LDR shunt attenuator; hyperbolic law gives the knee)
r         = 20·log10(1 + R_s·(G_f + G_s))
```

- w = 0.5 matches "50 % in 60 ms".
- The opto's fastest constant, about 10 ms, makes the naive delayed loop exact enough: at 44.1 kHz,
  α_on = 0.99773, so any k ≤ 440 is non-ringing. **Opto needs no ZDF.**
- The steady-state curve for the UI uses the monotone solve of §6.

### 2.8 Black-box and grey-box modelling: how it feeds the engine

- **Eichas & Zölzer (SPIE 2016; AES 142 2017)**: a generic block-oriented model with a peak detector (state- and
  sign-dependent coefficients), a static curve LUT with g_pre/g_post, and a three-one-pole smoothing filter. Parameters
  are found by Levenberg–Marquardt on an AM-sine envelope cost, then on real signals, then on time-domain MSE [P].
  **Our stage slots are a superset of this structure**, so the same fitting approach sets Mode constants.
- **Wright & Välimäki (DAFx20in22)**: the traditional compressor structure made differentiable and trained end to
  end on LA-2A [P]. **Steinmetz & Reiss (AES 2022, arXiv 2102.06200)**: a TCN black box for LA-2A [P]. Not for
  real-time use here. Useful as reference renders.
- **Newton–Raphson LA-2A sound matching (arXiv 2509.10706, 2025)**: fits a 5-parameter FF compressor per Peak
  Reduction setting in under 20 minutes. It reports ESR about 7–8 % and beats commercial emulations on its metric [P].
  This shows even a simple FF structure with fitted per-setting constants gets close.
- **Control-voltage evaluation (arXiv 2606.18573, 2026)**: models trained on the *gain-control signal* beat
  proxy-loss models on the actual control trajectory [P]. For us: **the applied-GR trace (tap/telemetry) is the right
  quantity for C's D2 probe and for any fitting harness**, not the audio output.
- **Solid State Bus-Comp dataset (arXiv 2504.04589)**: 220 SSL G combinations. Threshold −28/−24/−20/−16 dB, attack
  0.1/0.3/1/3 ms, release 0.1/0.4/0.8 s or Auto, ratio 2/4/10 [P]. Usable as a fitting and validation corpus for Bus G.

**Recommendation:** a `Tools/ModeFit` harness (later sprint) that fits a Mode's `[H]` constants by Levenberg–Marquardt
or Newton against captures. It runs the real `ModeEngine<Traits>` and compares GR traces.

### 2.9 Colour and aliasing

**ADAA-1** [P Parker, Zavalishin & Le Bivic, DAFx-16, eq. 9–10]:

```
y[n] = (F0(x[n]) − F0(x[n−1])) / (x[n] − x[n−1])            F0 = antiderivative of f, with F0(0) = 0
if |x[n] − x[n−1]| < ε:  y[n] = f((x[n] + x[n−1])/2)           error O(Δ²)
```

- The rectangular kernel adds a **half-sample delay**.
- At 50 % mix that half sample combs against the undelayed dry signal: −0.69 dB at 12 kHz and −2.0 dB at 20 kHz, at
  48 kHz [D].
- **Fix:** apply ADAA only to the residual `f(x) − x`. The linear part stays undelayed and only the (small)
  distortion products carry the half-sample shift.
- Antiderivatives:
  - tanh: `log cosh x = |x| + log1p(e^(−2|x|)) − ln 2`
  - hard clip: `x²/2` inside the limits and `|x| − 1/2` outside.
- Time-varying drive (GR-dependent colour) breaks the ADAA assumption only to second order, because drive moves at
  control rate.

**JUCE 8.0.4 `dsp::Oversampling`** (`juce_Oversampling.h:98-123`). Measured latency is in base-rate samples; the
integer-latency variant is in parentheses. Cost is up plus down, per base-rate sample per channel, stereo, with no
processing in between, at -O2 -mcpu=apple-m1 [M]:

| Filter | Quality | 2× | 4× | 8× |
|---|---|---|---|---|
| FIR equiripple | normal | 31 (31); 21.3 ns | 38 (38); 68.9 ns | 41 (41); 129 ns |
| FIR equiripple | max | 49 (49); 54.1 ns | 59.5 (61); 93.5 ns | 64.25 (65); 211 ns |
| Polyphase IIR | normal | 2.34 (3); 10.9 ns | 3.42 (5); 51.0 ns | 3.83 (5); 112 ns |
| Polyphase IIR | max | 3.14 (4); 19.3 ns | 4.43 (6); 52.1 ns | 4.95 (6); 107 ns |

- The first stage's transition width is `(maxQ ? 0.10 : 0.12)·0.5`; stop-band attenuation is −90/−70 dB going up and
  −75/−60 dB going down (`juce_Oversampling.cpp:564-590`).
- **The IIR's phase is non-linear.** That is harmless *only* if dry and wet share the same up/down path, which is why
  the mix happens inside the oversampled domain (§5.3).

---

## 3. Engine decomposition

### 3.1 Pipeline

```
            ┌──────────── Mode-agnostic host (CompressorEngine) ────────────┐   ┌── ModeEngine<Traits> (per Mode) ──┐
 main L/R ─►│ route & M/S encode ─► lookahead delay L_la ─► [OS up] ─────────┼──►│ gain element × g ─► Colour (ADAA/ │
            │                                                               │   │  OS) ─► makeup                    │
 key L/R  ─►│ SC source select ─► M/S encode ─► SC HPF (TPT SVF) ─► SC tilt ┼──►│ Detector ─► GainComputer ─► Link  │
            │                                                               │   │ ─► Ballistics ─► [Stage2] ─► r[n] │
            │                                                               │   │ (FB: fused ZDF loop)              │
            │ dry = delayed input at OS rate ───────────────────────────────┼─► mix(dry, wet) ─► [OS down] ─► M/S   │
            │ decode ─► output trim ─► bypass/SC-listen ramps ─► meters/UiFrame/tap                                 │
            └──────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

The host owns everything whose behaviour must be identical across Modes:
- routing, M/S, SC filters (D's `schpf`, and `sce` when continuous);
- delays, oversampling, mix, output, bypass, SC listen, telemetry and crossfades.

The Mode owns the dynamics, the gain law and the colour.

### 3.2 SIMD layout and `fcmp::simd`

- **Lanes = channels.** `f32x4 = {ch0, ch1, aux0, aux1}` with ch = L/R, or M/S after encoding. aux0 and aux1 carry
  *independent* recurrences that would otherwise cost their own chain:
  - the crest-factor peak and RMS detectors (§2.5a);
  - the Stage-2 detector and ballistics of a two-stage Mode (D F11);
  - a second link-shape detector (API FAST/SLOW).

  Serial chains, such as DualRelease's r_f → r_s, **cannot** be packed.
- Why not vectorise across time: the recursions (ballistics, RMS, SC filters, FB loop) are serial. Channel lanes cost
  nothing extra, and [M] showed the loop is latency-bound at about 3.9 ns per stereo frame. Stateless per-sample work
  (level → dB, gain → linear, colour) *can* also be vectorised across time in the audio pass, 4 samples of one channel
  per vector. That is optional.
- **Ops to add to HR's set** (Simd.h has load/store/set1/add/sub/mul/fma/fms/rsqrte/rsqrts):

| op | NEON | x86 (AVX2+FMA build) | Note |
|---|---|---|---|
| `min`/`max` | `vminq_f32`/`vmaxq_f32` | `_mm_min_ps`/`_mm_max_ps` | NaN rules differ (NEON propagates NaN; x86 returns the 2nd operand). Keep NaNs out: poison check, floors. |
| `abs` | `vabsq_f32` | `_mm_andnot_ps(-0.f, a)` | exact |
| `div`, `sqrt` | `vdivq_f32`, `vsqrtq_f32` | `_mm_div_ps`, `_mm_sqrt_ps` | IEEE correctly rounded, so bit-equal across arches |
| `floor` | `vrndmq_f32` | `_mm_floor_ps` (SSE4.1, within the AVX2 baseline) | exact |
| `gt`, `sel` | `vcgtq_f32`, `vbslq_f32` | `_mm_cmpgt_ps`, `_mm_blendv_ps(f,t,m)` | exact |
| `log2`, `exp2` | polynomial via `fma` | same polynomial via `fma` | identical rounding when written only with `fma()`. Truncating converts (`vcvtq_s32_f32` / `_mm_cvttps_epi32`) agree on floored input |

[M] Accuracy of the prototype polynomials:
- 5th-order `log2` on the mantissa: max error 3.8e-6 log2 units = 2.3e-5 dB.
- 5th-order `exp2` on the fraction plus exponent add: max relative error 8.5e-5 = 7.4e-4 dB.

Refit with a minimax tool before shipping to get about 10× better at the same cost.

### 3.3 Stage catalogue

Every stage is a small POD `State` of `f32x4` members plus a `Coeffs` struct, with a `static inline` `tick`.
`Coeffs::design(const Resolved&, float fs)` runs on control ticks only (§4.6).

| Slot | Policies | State per lane | Recompute at tick |
|---|---|---|---|
| **Detector** | `PeakLog` (instantaneous, GR-domain smoothing does the rest), `RmsLog(τ)`, `PeakLinRC(τA,τR)` (analog rectifier+RC, linear domain), `DualDet` (peak and RMS in aux lanes), `SignSplit` (Eichas sign-dependent coefficients) | 0–2 floats | α via exp2 |
| **GainComputer** (pure) | `QuadKnee{T,W,S}`, `ProgressiveKnee{T,S_max,o_c}`, `TableCurve{pchip, g_pre, g_post}`, `Upward{…}` (r̂ < 0 below T, D F17), plus `range` clamp `r̂ ≤ range` | none | T/W/S smoothing |
| **Link** | `Independent`, `Max`, `Mean`, `CvSumOwnDetector` (API style), all blended by `link` k: `r̂_ch += k·(combine(r̂) − r̂_ch)` | none | – |
| **Ballistics** | `SmoothBranching`, `SmoothDecoupled`, `DualRelease`, `RateRelease(ρ)`, `CrestAuto<Inner>`, `MultiStage3`, `Hold<Inner>` (hold counter before release), `OptoCell`, `TcSelector` (Fairchild TC table → α set) | 1–4 floats | α's |
| **Topology** | `FeedForward`; `FeedbackZdf<Computer>` (closed form for Quad/hard, 2-step Newton otherwise); `FeedbackDelayed` (allowed only if `k ≤ α/(1−α)` for the loop's fastest α, which `static_assert`/debug-asserts) | 0 | – |
| **Stage2** (D F11) | `None`, `SharedElementMax` (r = max(r1, r2): diode-bridge 33609/2254), `SerialPre<ModeTraits>` (a whole second chain before the first), `PostClip<Shaper>` (ADAA clipper) | per inner | – |
| **Colour** | `Clean`, `VcaBus`, `FetColor` (2nd harmonic ∝ signal at the FET × (1−g)) [H], `TubePushPull` (odd, grows with GR), `Transformer` (pre-emphasis → shaper → de-emphasis, so LF saturates first), `DiodeBridge`; run as ADAA residual or at OS rate | 1–4 floats | drive |

Gain-element law (the linear gain as a function of the control state) belongs to the Topology/Ballistics pair:
- VCA: `g = exp2(−r/6.02)`
- LDR shunt: `g = 1/(1 + R_s·G)`
- FET VCR: `g = R_fet/(R_in + R_fet)` [H]
- vari-mu: gm(bias) [H]

Everything is expressed as an equivalent r in dB, so telemetry, link and the UI see one unit.

### 3.4 Block structure (FF vs FB), chunked at 64 samples

- **FF:**
  1. The host fills `sc[64]` (`f32x4`, filtered, encoded).
  2. The Mode's control pass (detector → GC → link → ballistics → Stage2) writes `r[64]` and, when the tap or telemetry
     is on, `lvl[64]` and `tgt[64]`.
  3. The host audio pass computes `g = exp2((M − r)/6.02)` (linearly interpolated to the OS rate when oversampling),
     applies it to the delayed main signal, runs the Mode's colour, mixes and downsamples.

  Lookahead falls out naturally: the control path reads the undelayed SC and the audio is delayed.
- **FB:** the control loop is one fused per-sample loop using the ZDF solve. It senses `ℓ(filter(x)) − r` (§2.6), so
  it still runs at the control rate on base-rate SC samples. Colour runs after the loop, in the audio pass. If a Mode
  declares `colorInLoop` (the detector hears the coloured output), the loop moves into the OS-rate audio pass. That
  costs about 4 ns × factor per channel.
- The per-sample buffers **are** C's test tap (`FCMP_TEST_TAP` exports them) and the telemetry source (§7). There is
  no second instrumentation path.

### 3.5 Code sketch: stages, traits, engine, registry (C++20)

```cpp
// Source/dsp/stages/Ballistics.h
namespace fcmp::stage {
using simd::f32x4;

struct SmoothBranching {
    struct Coeffs { f32x4 aA, aR; };                    // α = exp2(-1/(τ·fs·ln2))
    struct State  { f32x4 r = simd::set1(0.0f); };
    static f32x4 tick(const Coeffs& c, State& s, f32x4 rhat) noexcept {
        const f32x4 a = simd::sel(simd::gt(rhat, s.r), c.aA, c.aR);
        s.r = simd::fma(rhat, a, simd::sub(s.r, rhat)); // rhat + a·(r − rhat)
        return s.r;
    }
    static void seed(State& s, f32x4 r) noexcept { s.r = r; }
};

struct DualRelease {                                    // SSL-style Auto (§2.5b)
    struct Coeffs { f32x4 aA, aRf, aC, aRs; };
    struct State  { f32x4 rf = simd::set1(0.0f), rs = simd::set1(0.0f); };
    static f32x4 tick(const Coeffs& c, State& s, f32x4 rhat) noexcept {
        s.rf = simd::fma(rhat, simd::sel(simd::gt(rhat, s.rf), c.aA, c.aRf), simd::sub(s.rf, rhat));
        s.rs = simd::fma(s.rf, simd::sel(simd::gt(s.rf, s.rs), c.aC, c.aRs), simd::sub(s.rs, s.rf));
        return simd::max(s.rf, s.rs);
    }
    static void seed(State& s, f32x4 r) noexcept { s.rf = r; s.rs = r; }
};
} // namespace fcmp::stage

// Source/dsp/stages/GainComputer.h: ONE implementation. The UI calls it with 4 abscissae per call.
struct QuadKnee {
    struct Coeffs { f32x4 T, halfW, W, inv2W, S; };     // W >= 1e-4
    static f32x4 target(const Coeffs& c, f32x4 x) noexcept {
        const f32x4 o = simd::sub(x, c.T);
        const f32x4 q = simd::min(c.W, simd::max(simd::set1(0.0f), simd::add(o, c.halfW)));
        const f32x4 lin = simd::max(simd::set1(0.0f), simd::sub(o, c.halfW));
        return simd::mul(c.S, simd::fma(lin, simd::mul(q, q), c.inv2W)); // S·(lin + q²/(2W))
    }
};

// Source/dsp/stages/Feedback.h: scalar form shown. The lane version replaces the ifs with sel().
inline float zdfQuadKnee(float x, float r1, float a, float T, float W, float k) noexcept {
    const float r = (a * r1 + (1.0f - a) * k * (x - T)) / (1.0f + (1.0f - a) * k);
    if (x - r - T >= 0.5f * W) return r;                               // linear region
    const float b = x - T + 0.5f * W, c = b - a * r1;
    if (c > 0.0f) {
        const float kap = (1.0f - a) * k / (2.0f * W);
        const float u = 2.0f * c / (1.0f + std::sqrt(1.0f + 4.0f * kap * c));
        if (u <= W) return b - u;                                      // knee region
    }
    return a * r1;                                                     // below knee
}   // static FB curve: zdfQuadKnee(x, 0, 0, T, W, k)

// Source/modes/BusG.h: one Mode = one header (D §6.3 parallel split)
struct BusG {
    static constexpr const ModeSpec& spec = kBusGSpec;                 // §4.3
    using Detector    = stage::PeakLog;
    using Computer    = stage::QuadKnee;
    using Link        = stage::LinkMax;
    using Ballistics  = stage::AutoSwitch<stage::SmoothBranching, stage::DualRelease>; // tag AUTO picks the 2nd
    using Stage2      = stage::None;
    using Colour      = colour::VcaBus;
    static constexpr Topology topology = Topology::FeedForward;
    static void resolve(const RawParams&, Resolved&) noexcept;         // snaps + derived W = f(T, R) [H]
};

// Source/dsp/Engine.h
struct Carry { simd::f32x4 grDb, detDb; };                             // Mode-switch state hand-over
class IEngine {
public:
    virtual ~IEngine() = default;
    virtual void  prepare(const PrepareInfo&) noexcept = 0;            // RT-safe: math only, no allocation
    virtual void  reset() noexcept = 0;
    virtual Carry carry() const noexcept = 0;
    virtual void  seed(const Carry&) noexcept = 0;
    virtual void  control(const ControlIo&, const Resolved&) noexcept = 0;  // sc[] → r[] (+ lvl/tgt taps)
    virtual void  colour(float* const* audio, int nOs, const Resolved&) noexcept = 0; // in place, OS rate
    virtual void  internals(float out[16]) const noexcept = 0;         // meanings: ModeSpec::internals
};

template <class M>
class ModeEngine final : public IEngine {
    alignas(16) typename M::Detector::State   det_{};
    alignas(16) typename M::Ballistics::State bal_{};
    alignas(16) typename M::Colour::State     col_{};
    typename M::Detector::Coeffs dc_{}; typename M::Computer::Coeffs gc_{};
    typename M::Ballistics::Coeffs bc_{}; typename M::Colour::Coeffs cc_{};
    ControlTicker tick_{};                                             // absolute-index tick, every 16 samples
public:
    void control(const ControlIo& io, const Resolved& p) noexcept override {
        for (int n = 0; n < io.n; ++n) {
            if (tick_.advance()) design(p);                            // coefficients from smoothed values
            if constexpr (M::topology == Topology::FeedForward) {
                const auto lvl = M::Detector::tick(dc_, det_, io.sc[n]);
                const auto tgt = M::Link::apply(M::Computer::target(gc_, lvl), io.link);
                io.r[n] = M::Ballistics::tick(bc_, bal_, tgt);
                if (io.tap) io.tap->put(n, lvl, tgt);
            } else {
                io.r[n] = feedbackStep(io.sc[n]);                      // §2.6 ZDF on ℓ(filter(x)) − r
            }
        }
    }
    // carry/seed/reset/colour/internals: trivial forwards to the policies
};

// Source/modes/Registry.cpp: X-macro from Modes.def (C §5.14)
struct ModeEntry {
    const ModeSpec* spec;
    IEngine* (*construct)(void* arena) noexcept;                       // placement-new, RT-safe
    void (*staticCurve)(const Resolved&, const float* xDb, float* yDb, int n) noexcept; // same template code
};
template <class M> constexpr ModeEntry entryFor() noexcept {
    static_assert(sizeof(ModeEngine<M>) <= kArenaBytes && alignof(ModeEngine<M>) <= 64);
    // Rule: ModeEngine owns no resources (POD state only), so the old engine is dropped with a plain ~IEngine() call.
    return { &M::spec, [](void* p) noexcept -> IEngine* { return new (p) ModeEngine<M>(); },
             &ModeEngine<M>::staticCurveImpl };
}
```

### 3.6 Static vs dynamic dispatch

| Option | Cost | Verdict |
|---|---|---|
| virtual per sample or per stage | kills inlining and SIMD, ~5–10× slower | no |
| `std::variant<Engine<A>, …>` + `std::visit` per block | same speed as below | closed set; every Mode addition recompiles every TU that sees the variant; state size = the largest Mode |
| **virtual per block over `ModeEngine<Traits>`** | 1 indirect call per 64-sample chunk | **yes**: open registry, one Mode per header, arena-sized |
| stage-level virtuals with runtime composition | per-sample virtuals | no |

**Memory:**
- Two arena slots of `kArenaBytes = 8 KiB` each, `alignas(64)`, in the host object. A Mode engine's state is well
  under 1 KiB (a few dozen `f32x4`), so 8 KiB leaves room for table curves: 97-point PCHIP ≈ 1.5 KiB.
- Host buffers, all sized in `prepareToPlay`:
  - chunk scratch: 64 × `f32x4` × 6 ≈ 6 KiB;
  - lookahead ring: 20 ms × 192 kHz × 2 ch → 2 × 4096 floats, power of two;
  - dry ring at OS rate;
  - JUCE Oversampling.

### 3.7 Cost targets (grounded in [M])

| Item | Measured / target, ns per base-rate sample per channel |
|---|---|
| Core FF control path (NEON, 2 lanes) | **1.95 measured**; target ≤ 3 |
| Scalar libm reference | 9.29 measured (do not ship) |
| FB ZDF control path | target ≤ 6 (a divide and a sqrt per sample) |
| SC HPF (TPT SVF) + tilt (6 first-order sections) | target ≤ 3 |
| Audio pass, no OS (exp2, apply, clean colour, mix) | target ≤ 3 |
| ADAA-1 colour at base rate | target ≤ 5 |
| 2× IIR max-Q up+down | 19.3 measured |
| 4× FIR max-Q up+down | 93.5 measured |
| **Budget per Mode, Std quality (2× IIR)** | ≤ 40 ns → 2 ch × 48 kHz × 40 ns = **0.38 % of one core** |
| **Budget, HQ (4× FIR)** | ≤ 150 ns → 1.4 % of one core |
| During a Mode crossfade (20 ms) | the control and colour cost doubles; the OS cost does not |

C's `ctBudgetNsPerSample` per Mode should take these numbers. Add a `Tools/DspBench` row per Mode at 48 and 96 kHz,
following HR's `Tools/DspBench.cpp` pattern.

### 3.8 Denormals, floors, poison

- `juce::ScopedNoDenormals` at the top of `render` (HR PluginProcessor.cpp:346). It sets FZ in FPCR on arm64 and
  FTZ/DAZ on x86. Harnesses use `ScopedFtz` (Tools/Harness.h:50).
- Log-domain floors: `max(|s|, 1e-12)` and `max(ms, 1e-24)` before `log2`, so ℓ ≥ −240 dB and never −inf.
- GR-domain states decay toward 0 dB GR, which is a normal float, so they never go denormal. The linear-domain states
  (SC filters, RMS ms, opto G) rely on FTZ.
- Once per block, check `isfinite` on the engine's `carry()` lanes, SC filter states and the last output sample. On
  failure, reset the engine and host filters (FDNReverb.cpp:1263-1267).

---

## 4. Parameter model

### 4.1 Options compared

| Criterion | (i) One universal set, filtered by the active Mode's ParamSpec | (ii) Per-Mode parameter sets | (iii) Hybrid: universal + a few Mode-defined generic slots |
|---|---|---|---|
| Host parameter count | fixed forever (~24 + Stage 2) | grows by about 10 per Mode; hosts cache lists (AU `kAudioUnitProperty_ParameterList`) | fixed |
| Automation meaning after a Mode switch | carries over, snapped | a lane for Mode A does nothing in Mode B | carries over |
| Adding a Mode | no host change | host list changes; Logic and others need a rescan | none |
| A→B→A restores A's values | yes, if the raw value is kept | yes | yes |
| Presets | one flat set + `modeId` | huge, mostly dead | flat |
| Undo | one APVTS tree; a Mode change is one value | many | one |
| Display text | `stringFromValue` reads the active Mode | static | dynamic |
| Complexity | resolver + ParamSpec | trivial per Mode, painful in aggregate | as (i) plus labels for generic slots |

**Recommendation: (i), with (iii)'s generic slots.** Those are D's `tmode`, `det`, `topo`, `lshape` and `color`:
stepped lists whose labels the Mode supplies. That is exactly D §4 and B §7.4.1–2.

### 4.2 Rules

1. **Raw is truth; snap on read.** `resolve(spec, raw) → Resolved` is a pure function. It is called on:
   - the audio thread, once per block;
   - the UI thread, for knobs and curves;
   - `stringFromValue`.

   A Mode change never writes parameters, so A→B→A restores A exactly and undo is a single Mode value. **This changes
   B §7.4.2**, which snaps raw values on a UI Mode change. The cost of keeping raw: a host lane's *position* can sit
   between detents while its *text* shows the snapped value. That is acceptable, and HR's segmented cells draw the
   snapped state. B's behaviour can be a UiPreference ("Normalise parameters on Mode change").
2. **Snap on write in the UI.**
   - Stepped knobs move in step-index space with detents, then write the step's canonical plain value through
     `convertTo0to1` inside a gesture (B §7.4.2), so recorded automation holds exact detents.
   - Composite knobs write several parameters inside one gesture (D §5.7): release + `tmode` = Auto, and the TC
     selector.
3. **Snap domain per parameter:**
   - Times: log domain, so boundaries fall at geometric means.
   - **Ratio: the slope domain S.** Boundaries are midpoints in S. For SSL 2/4/10 that is S = 0.625 (2.67:1) and
     0.825 (5.7:1). For 10 → ∞ it is S = 0.95 (20:1). This handles ∞ and negative steps with no special cases.
   - dB values: linear.
   - Ties go to the lower step. No hysteresis, so snapping is deterministic, which C's D3 probe requires.
4. **Kinds** (D §5.7):

   | Kind | Behaviour |
   |---|---|
   | Continuous | clamp to the Mode's lo/hi |
   | Stepped | nearest `Step{plain,label,tag}` |
   | Fixed | the Mode's value; UI ghosted |
   | Program / Derived | read-only; the value comes from other parameters, e.g. Fairchild attack from TC |
   | Unused | host value ignored; neutral engine value |
   | Remapped | a different label and scale over the same slot, e.g. "Peak Reduction 0–100" onto `thr` |

   - `tag` bits carry behaviour: AUTO, ALL (all-buttons), TCn, VAR.
   - `dependsOn` and `altSteps` give conditional lists (D §5.7).
5. **Extension parameters.** SC HPF, tilt, link, M/S, mix and range are "+" cells (D §5.7 `extension`). They are
   available on every Mode unless the Mode opts out, with the neutral value as default. This keeps modern conveniences
   everywhere without altering vintage behaviour when left at neutral.

### 4.3 Host mapping and text (JUCE 8.0.4)

- **Ratio:** store S ∈ [0, 2] in an `AudioParameterFloat` with a custom `NormalisableRange` built from the
  three-lambda constructor. `ValueRemapFunction = std::function<float(float start, float end, float v)>`
  (juce_NormalisableRange.h:104-106).
  - Suggested taper: v ∈ [0, 0.8] → R = 20^(v/0.8) (1→20:1), v ∈ (0.8, 0.9] → S from 0.95 to 1 (20:1 → ∞),
    v ∈ (0.9, 1] → S from 1 to 2 (∞ → −1:1).
  - Text: `R = 1/(1−S)` → "4:1", "∞:1", "−2:1", or the step label.
- **Text:** `AudioParameterFloatAttributes().withStringFromValueFunction(...)` and `.withValueFromStringFunction(...)`.
  The lambda loads `activeMode_` (an atomic slot) and indexes the constexpr ModeSpec table, which is safe from any
  thread. It returns the snapped value's label, "—" for Unused, or the fixed or derived value in parentheses.
- **Mode change:** on the message thread, call `updateHostDisplay(ChangeDetails().withParameterInfoChanged(true))`.
  - VST3: JUCE re-reads names and step counts and raises `kParamTitlesChanged`
    (juce_audio_plugin_client_VST3.cpp:857-882, 1371-1376).
  - AU: JUCE posts `kAudioUnitProperty_ParameterList` and `kAudioUnitProperty_ParameterInfo`
    (juce_audio_plugin_client_AU_1.mm:1946-1974).
  - **Do not vary `getNumSteps()` per Mode.** JUCE would forward it, but host support for changing step counts is
    inconsistent.
- **Mode parameter:** `AudioParameterInt("mode", 0..127)`.
  - Append-only slot table; `.withAutomatable(false)` is available (juce_AudioProcessorParameterWithID.h:96).
  - Unassigned slots resolve to the nearest assigned slot.
  - The stable string `modeId` goes in state and presets (B §7.4.3, C §5.7.4).
  - B proposes 64 slots. D already catalogues 35 Modes and the user says Modes will keep being added, so use 128. The
    normalised resolution of 1/127 costs nothing.
- **Undo:** give the APVTS an `UndoManager` (HR passes `nullptr`, PluginProcessor.cpp:211). A Mode switch plus an
  optional "load Mode defaults" is one transaction (`beginNewTransaction`).
- **Per-Mode memory** (optional UX): a state-only map `modeId → raw snapshot`, applied only when the user enables
  "Recall per-Mode settings". It is never a host parameter.

### 4.4 `Resolved`: what the engine and UI consume

```cpp
struct Resolved {                    // natural units, after snap + Mode remaps + derived values
    float inDb, thrDb, S, kneeDb, rangeDb;       // S = slope (FB converts to k = S/(1−S))
    float atkMs, relMs, holdMs, lookMs;
    float makeupDb, outDb, mix;                  // mix 0..1 (0..2 on digital Modes)
    float scHpfHz, scTiltDbOct, link;
    uint8_t tmode, det, topo, lshape, color, stereoMode;
    uint32_t tags;                               // OR of the active steps' tags (AUTO, ALL, TC5, ...)
    Stage2Params s2;                             // D §4 s2thr/s2atk/s2rel
};
```

### 4.5 Remaps that change meaning

These go in `M::resolve`, not in the engine (D §3):
- **Input drives a fixed threshold** (1176): `thr` is Fixed at the unit's internal threshold. `in` feeds both the
  colour drive and the level.
- **Side-chain gain as threshold** (LA-2A Peak Reduction): Remapped `thr` 0–100 → side-chain gain (dB) → the loop's
  effective threshold.
- **Threshold ↔ ratio coupling** (1176 raises T with ratio; Altec 436C): a `thresholdOffsetFromRatio` hook.
- **Coupled times** (Fairchild TC): `rel` is Stepped with TC1..TC6 labels, and `atk` is Derived.

### 4.6 Smoothing: block-size invariant (C §5.6.3), unlike HR

- **Per-sample one-poles** on the continuous resolved values, packed four per `f32x4`:
  - {T, S, W, range}
  - {makeup, output, mix, input}

  τ = 20 ms, α precomputed from fs. The epsilon landing follows HR's `smoothSnap` (FDNReverb.cpp:750). Cost: 2 FMAs
  per sample.
- **Times** (attack/release/hold): smoothed in the log-time domain on **control ticks every 16 samples at the absolute
  sample index** (`ControlTicker`). The ballistics α values are recomputed there with the poly `exp2`. Coefficient
  changes need no ramp, because a one-pole's state is continuous across a coefficient step.
- **Stepped discrete behaviour.** Tags such as ALL or AUTO switch at the next control tick. Continuous values that a
  tag drives (colour drive, knee) pass through the smoothers.
- A change that swaps the **kernel** (API `topo` FF↔FB, `det` Peak↔RMS on a Mode that compiles two kernels) is
  handled like a Mode switch: engine crossfade (§5.1).
- Preset recall snaps the smoothers (HR's `snapPending_` / `requestParameterSnap` acquire/release pattern). A preset
  that changes Mode still crossfades, but the new engine starts at its snapped targets.

---

## 5. Mode switching, latency, lookahead, oversampling

### 5.1 Click-free switch

- The message thread writes the Mode parameter. At the next block start the audio thread sees `slot != current`:
  1. Placement-construct `ModeEngine<New>` in the idle arena slot. That is POD initialisation plus `prepare` (a few
     `exp2`), bounded and allocation-free.
  2. `seed(old.carry())`: the new ballistics state takes the old **applied GR in dB per lane**, and the new detector
     takes the old detector level, each in its own domain (dB → ms for RMS, → G for opto via the inverse law).
  3. Run both engines on the same input for **20 ms**. Output = `(1 − w)·out_old + w·out_new` with a per-sample linear
     w. Use an equal-*gain* crossfade, because the two outputs are coherent. Equal-power would bump +3 dB mid-fade.
  4. Destroy the old engine (trivial) and swap the pointers.
- A request that arrives during a fade is latched and applied when the fade ends (≤ 20 ms). Only the latest request is
  kept.
- Because latency and oversampling are identical for every Mode (§5.2), the two outputs are sample-aligned, and C's
  D4 probe `latency_constant = 1` holds by construction.
- Seeding keeps `level_dev_db` (C §5.5) small. Without it the new Mode would re-attack from 0 dB GR, which is an
  audible swell.

### 5.2 Latency policy

| Policy | Problem | Verdict |
|---|---|---|
| Per-Mode latency, calling `setLatencySamples` on a Mode switch | hosts re-sync PDC with a dropout; some only read latency at start; a Mode switch becomes a time jump (fails C D4) | no |
| Max over all Modes | grows as Modes are added, and penalises every Mode | no |
| **f(global Quality, Lookahead-enable) only** | latency changes only on an explicit, non-automatable setup change | **yes** |

- `L_total = L_la,max·[lookahead enabled] + L_os(Quality)`, reported with `setLatencySamples` (juce_AudioProcessor.h:850)
  from the message thread.
- **Quality** is a global, non-automatable preference:

  | Quality | Processing | Latency at 44.1–192 kHz |
  |---|---|---|
  | Eco | no OS; ADAA residual colour | 0 samples |
  | **Std (default)** | 2× polyphase IIR, max-Q, integer latency | **4 samples** (83 µs at 48 kHz) [M] |
  | HQ | 4× FIR, max-Q, integer latency | 61 samples (1.27 ms at 48 kHz) [M] |

  Modes that do not need OS in Std still run the audio pass through the up/down filters, which cost 19 ns. The
  alternative is an integer delay of 4 samples, but only the filter path gives dry/wet phase identity. Take the
  filters.
- **Lookahead:**
  - Off by default. When the user enables it, latency becomes `L_la,max` = 5 ms or 20 ms (D §4 `look` 0–20 ms). This
    is a setup choice, not automatable.
  - The `look` parameter (0..L_la,max) is realised by delaying the *side chain* by `L_la,max − look` and the audio by
    `L_la,max`, so moving `look` never changes latency.
  - Modes whose `look` is Unused (hardware) use look = 0, with the same padded delay.
- `getTailLengthSeconds`: the longest release in the active Mode × 5, plus the latency. Opto memory counts too, e.g.
  15 s.

### 5.3 Oversampling and mix, in order

`x_d = delay(x, L_la)` → up(x_d) → wet = colour(up(x_d)·g_up) → `mix(wet, up(x_d))` → down → M/S decode → output trim
→ bypass/listen ramps.

- `g_up` is the control-rate gain linearly interpolated in dB, then `exp2` at the OS rate.
- Mixing before the downsampler means dry and wet see the *same* anti-imaging and anti-aliasing filters, so the IIR's
  non-linear phase cannot comb.
- The bypass ramp crossfades against the **latency-aligned dry** signal (B §7.4.5).

### 5.4 Brickwall and lookahead limiter (D #8, M30)

```
r_req[n] = max(0, ℓ_tp[n] − Ceiling)          ℓ_tp: true peak, 4× polyphase (ITU-R BS.1770 annex 2 method)
h[n]     = max(r_req[n−L..n])                 sliding max, O(1) amortised (monotonic deque or van Herk/Gil-Werman)
a[n]     = mean(h[n−L..n])                    box of length L+1 (or two cascaded boxes of L/2 for a smoother onset)
r[n]     = max(a[n], release one-pole(r[n−1]))
audio delayed by L
```

The box average of the max-held signal reaches r_req by the time the peak leaves the delay line. Stage-2 limiters
(165A PeakStop, alpha soft clip) reuse the sliding-max block without lookahead.

---

## 6. Characteristics: one code path for DSP and UI

### 6.1 What every Mode exposes

These are pure functions with no engine state, all generated from `ModeEngine<M>` statics:

| Function | Implementation |
|---|---|
| `staticCurve(const Resolved&, const float* xDb, float* yDb, int n)` | FF: `M::Computer::target` over 4 abscissae per call (the *same* SIMD function the audio thread runs). FB: `zdf…(x, 0, α=0, …)` closed form, or the 2-step Newton with α = 0, iterated to convergence (≤ 6 steps) on the UI. Then `y = x − r + M` (+ colour describing-function gain, §6.3) |
| `localRatio(x)` | `1/(1 − dr̂/dx)`, by central difference on `staticCurve` (±0.05 dB) |
| `stepPreview(const Resolved&, const PreviewSpec&, float* grDb, int n)` | runs a **private `ModeEngine<M>`** on a synthetic 1 kHz tone burst |
| `timeSpec` / `attackSpec` / `releaseSpec` (C §5.1) | published times with `TimeLaw`, for probes and readouts |

- **Delta to C §5.1:** C's `staticGainDb(double, ParamSnapshot)` should *wrap* the float SIMD function (broadcast
  plus lane 0), not re-implement it in double. C's D1 probe keeps its own *independent* textbook formula as the second
  opinion (C §5.2), which is the right check.
- **Program-dependent Modes** (Opto, TC5/6, Auto). The static curve is the infinite-duration steady state. The
  preview plot is the honest picture: D §7 asks for GR-vs-time for a standard burst.

### 6.2 Axis semantics and operating point

- The x-axis is the **detector-domain level**: what the gain computer sees, calibrated per `detector` (peak = sine
  peak, RMS = peak − 3.01 dB). The UI labels it.
- Operating point:
  - a **live dot** at `(detDb, detDb − appliedGr + M)`, which sits off the curve during attack and release and makes
    the ballistics lag visible;
  - a **target dot** on the curve at `(detDb, detDb − targetGr + M)`.

### 6.3 Steady-state curves for FB and level-dependent designs

- **FB:** solve `F(r) = r − r̂_fb(x − r) = 0`. It is monotone, so Newton from r0 = 0 works, with a bisection bracket
  `[0, max(0, x − T + W)]`. Quad and hard knees have the closed form (α = 0 in §2.6).
- **Linear-domain gain laws** (opto `1/(1+R_s·G)`, FET VCR): solve in the state variable (G or e), then convert to r.
- **Colour in the static curve:** a saturating stage compresses level too. Its sine-level gain is the describing
  function `N(A) = (1/(πA))·∫₀^{2π} f(A·sinθ)·sinθ dθ`, evaluated by a 64-point trapezoid (exact for polynomial
  shapers up to high order) at the amplitude *after* the gain element. The curve then adds `20·log10 N(A)`. This
  matches C's single-bin DFT method (fundamental only).
- **Measured overlay (optional):** a background thread runs the real engine at 61 sine levels (§6.4) and draws the
  measured points over the analytic curve. That is the visual form of C's D1 probe.

### 6.4 Preview cost and cadence

- Tone burst: 50 ms silence → L_hi = T + 12 dB (user-adjustable) for 500 ms → L_lo = T − 12 dB for 2 s. That is
  122 400 samples at 48 kHz.
- The burst is mono, because the curve is per lane. At ≤ 10 ns per sample per channel this is **≈ 1.2 ms per
  recompute** (a 20 µs FB Mode at 2× costs about 3 ms) [D from M].
- Recompute on a resolved-parameter change, throttled to ≤ 30 Hz, on the message thread or a worker. Decimate to the
  plot width with min/max columns.
- Readouts: t63 / t90 attack, t50 / t90 release, and for program-dependent Modes t50 after 1 s vs after 10 s of GR
  (C §5.3).
- 61-level static measurement: 61 × (0.3 s + 0.1 s) × 48 k × 10 ns ≈ 12 ms. Background only.

### 6.5 Drift-proof tests (additions to C §5)

- The FF `staticCurve` and the DSP's `M::Computer::target` are the same symbol. A unit test asserts bit equality over
  a grid, which catches anyone "optimising" one copy.
- For FB, the engine's settled GR at constant level equals `staticCurve` within 0.01 dB for peak-type Modes at
  release ≥ 100 ms (C §5.2).
- `stepPreview` versus the plugin render of the same burst must be bit-identical: same class, same inputs.

---

## 7. Telemetry to the UI (`UiFrame` + history ring)

The seqlock pattern is identical to HR's (FDNReverb.h:309-327: `static_assert(is_trivially_copyable)`, whole words,
`std::array<std::atomic<uint32_t>, kUiWords>`, staged copy, fences as in FDNReverb.cpp:1274-1336). It is published
once per processBlock while `uiAttached_` is set.

```cpp
struct UiFrame {                        // 228 bytes, 57 words
    uint32_t publishCount;
    uint16_t modeSlot; uint16_t flags;  // b0 bypass, b1 scListen, b2 midSide, b3 extKey, b4 fading, b5 clip
    float    sampleRate, latencySamples, fadeProgress;
    // Meters, per lane: block peak plus a 40 ms-release envelope (instant attack), as in HR publishUiFrame
    float inPeakDb[2], inRmsDb[2], outPeakDb[2], outRmsDb[2], scPeakDb[2];
    // Control path, end-of-block values per lane (L/R or M/S)
    float detDb[2];                     // x of the operating point (detector domain, §6.2)
    float targetGrDb[2];                // static-curve GR at detDb (no ballistics)
    float appliedGrDb[2];               // after ballistics + link + Stage 2: what multiplies the audio
    float blockMaxGrDb[2];              // largest GR this block (so short spikes are not lost)
    float s2GrDb[2];                    // Stage-2 GR (two-stage Modes), else 0
    float releaseNowMs[2];              // effective release τ right now (auto/program-dependent)
    float crest[2];                     // when CrestAuto is active
    float internals[16];                // Mode-defined; labels, units, ranges in ModeSpec::internals (B §7.5)
    // Resolved values the audio actually used (snapped + smoothed), so the UI draws exactly this curve
    float thrDb, S, kneeDb, rangeDb, atkMs, relMs, makeupDb, outDb, inDb, mix;
    uint32_t tags;
    uint32_t historyWritten;            // total columns pushed to the ring (monotonic): UI splice check
};
static_assert(std::is_trivially_copyable_v<UiFrame> && sizeof(UiFrame) % 4 == 0);
```

**History:** B §7.5 proposes an SPSC ring of fixed-rate points. Use **min/max columns** instead, so a 1 ms GR spike
between points is not lost.

```cpp
struct HistoryColumn { float grMinDb, grMaxDb, inPeakDb, outPeakDb; };   // over 1 ms (fs/1000 samples)
SpscRing<HistoryColumn, 2048> history_;   // ≈ 2 s at 1 kHz; the UI drains every frame (60–120 Hz)
```

- Columns are cut at absolute sample boundaries (fs/1000, rounded, with a fractional accumulator). The strip is then
  independent of the host block size.
- If the UI falls behind, the writer overwrites the oldest entries, and `historyWritten` lets the UI detect the gap.

**Internals by family** (`ModeSpec::internals` labels):

| Family | internals |
|---|---|
| VCA | fast release env, slow release env, "auto" blend = (r_s ≥ r_f), crest |
| Opto | light L, G_fast, G_slow, memory m, LDR "resistance" |
| FET | loop control voltage, FET resistance, 2nd/3rd harmonic level estimate |
| Vari-mu | bias V, local ratio, active TC stage weight |
| Two-stage | stage-1 GR, stage-2 GR, which stage wins |

**Rates and costs:**
- Per-sample accumulation (block peak, sum of squares, column min/max) runs only while attached. It reads the §3.4
  buffers, so the audio code gains no extra branches. The cost is under 1 ns per sample per channel.
- Publish is once per block, 57 relaxed word stores.

---

## 8. Side-chain features

- **External key bus** (B §7.4.7): `BusesProperties().withInput("Sidechain", stereo(), false)`.
  `isBusesLayoutSupported` accepts the key as disabled, mono or stereo.
  - Key source (global `Key input`: Internal/External) falls back to Internal when the bus is inactive.
  - A mono key is duplicated to both lanes.
  - FB Modes with an external key evaluate FF on the key (§2.6).
- **SC HPF:** a 2-pole Butterworth TPT SVF [P Zavalishin; Simper's trapezoidal SVF]. It is stable under per-sample
  coefficient changes.

  ```
  g = tan(π·fc/fs), k = √2, a1 = 1/(1 + g(g + k)), a2 = g·a1, a3 = g·a2
  v3 = v0 − ic2; v1 = a1·ic1 + a2·v3; v2 = ic2 + a2·ic1 + a3·v3
  ic1 = 2v1 − ic1; ic2 = 2v2 − ic2
  hp = v0 − k·v1 − v2
  ```

  Range: 20–500 Hz, with Off at the bottom of the host range (D §4). "Off" is an exact bypass (hp = v0), following
  HR's exact-neutral rule (FDNReverb.cpp `recomputeToneCoeffs`). Fixed internal SC coupling of a unit (for example
  the 33609 "Slow" 100 Hz HP) lives in the Mode's detector, not in this host filter.
- **SC tilt / emphasis** (D `sce`). A slope of σ dB/oct over 20 Hz–20 kHz, pivoted at 1 kHz = 0 dB, built from N
  first-order shelving sections, bilinear with prewarp:
  - section i has zero ω_z,i and pole ω_p,i with `ω_p/ω_z = 10^(Δ/20)`, where `Δ = σ·log2(ρ)` and ρ is the section
    spacing;
  - two sections per decade (ρ = 3.16, N = 6) gives about ±0.1 dB ripple [D, to be verified by C's probe];
  - σ < 0 swaps poles and zeros.

  API Thrust = +3.01 dB/oct (10 dB/decade) [P]. R37 or LA-3A HF emphasis and the Distressor 6 kHz "BE" are Mode-fixed
  shelf or bell presets of this same block.
- **SC listen:** outputs the post-filter, post-encode side chain, decoded, crossfaded with the 20 ms ramp, at unity
  gain. In M/S it can listen to M or S only.
- **Stereo link:** applied to the gain-computer *targets* before ballistics.

  ```
  r̂_ch += k·(combine(r̂_L, r̂_R) − r̂_ch)      combine = max (default) | mean | CV-sum (API)
  ```

  Linking by max of r̂ after the gain computer equals linking by max level before it, because the gain computer is
  monotone, and linking here lets the ballistics run per lane. API-style CV-sum keeps each channel's own detector
  (the manual's "each channel is still controlled by its own detector"). `lshape` FAST/SLOW selects whether the
  linked term comes from the peak or the RMS aux lanes.
- **M/S:** `M = (L+R)/2, S = (L−R)/2; L = M + S, R = M − S` (unity round trip). Both the SC and the audio are encoded,
  and link k applies between the M and S lanes (usually 0). Fairchild Lat/Vert is this with its labels (D §3).
- **Lookahead, oversampling:** §5.2–5.4.

---

## 9. Recommended engine, and what to build first

### 9.1 The design in one paragraph

A Mode-agnostic `CompressorEngine` owns:
- routing, M/S, SC HPF and tilt, lookahead and dry delays;
- `juce::dsp::Oversampling` (Quality-global, integer latency);
- mix inside the OS domain, output, bypass and SC-listen ramps;
- telemetry, and two arena slots.

A `ModeEngine<Traits>` in the active slot runs two things:
1. the control path in the dB-GR domain on `f32x4` channel lanes: Detector → pure GainComputer → Link → Ballistics →
   Stage 2, FF or ZDF-FB;
2. the Mode's colour at the audio rate.

It is called through one virtual call per 64-sample chunk. Parameters are the fixed universal set, resolved per block
by the Mode's pure `resolve()`, smoothed per sample, with time coefficients on 16-sample absolute ticks. Mode switches
crossfade over 20 ms with state carry-over. Latency never depends on the Mode. Curves and previews call the same
template code the audio thread runs.

### 9.2 Reusable building blocks, in order

Each block is a sprint-sized unit with its own test (C §5 names in brackets).

| # | Block | Files (proposed) | Test |
|---|---|---|---|
| 1 | `fcmp::simd` superset + poly `log2`/`exp2`, cross-arch bit-exact | `dsp/Simd.h` | exhaustive error scan; arm64 vs x86 bit-equality on 2³² bit patterns in chunks |
| 2 | Time and level helpers: `alphaFromTau`, `TimeLaw` conversions, dB↔lin | `dsp/Units.h` | table §2.3 round-trips |
| 3 | Parameter core: `Pid`, `ParamSpec`/`Step`/`Kind`/tags, `snap`, `resolve`, `format`/`parse`, ratio `NormalisableRange` lambdas | `params/ParamSpec.h`, `params/Resolve.cpp` | [D3] sweep, text round trip |
| 4 | Gain computers: `QuadKnee`, `ProgressiveKnee`, `TableCurve`(PCHIP), `Upward`, range clamp | `dsp/stages/GainComputer.h` | [D1] formula check vs the independent textbook implementation |
| 5 | Detectors: `PeakLog`, `RmsLog`, `PeakLinRC`, `DualDet` | `dsp/stages/Detector.h` | dbx numbers of §2.5c as a golden |
| 6 | Ballistics: `SmoothBranching`, `SmoothDecoupled`, `DualRelease`, `RateRelease`, `CrestAuto`, `MultiStage3`, `Hold` | `dsp/stages/Ballistics.h` | [D2] t63 / t10-90 per `TimeLaw` |
| 7 | Feedback: ZDF closed forms, 2-step Newton, static solve (α = 0), naive-loop guard | `dsp/stages/Feedback.h` | bisection comparison (as §2.6) plus the stability table as asserts |
| 8 | SC filters: TPT SVF HPF, tilt cascade, shelf/bell emphasis presets | `dsp/SideChainFilter.h` | magnitude vs target, exact bypass at Off |
| 9 | Router: lanes, M/S, key bus, link laws | `dsp/Router.h` | unity round trip; link k = 0/1 identities |
| 10 | Delays (lookahead, dry) + JUCE OS wrapper with mix-in-OS + ADAA residual shapers (tanh, asym-tanh, soft/hard clip) | `dsp/Delay.h`, `dsp/Oversampler.h`, `dsp/Adaa.h` | latency constant; dry-null at mix 0; alias level |
| 11 | Engine host: arena, registry (X-macro), crossfade + carry, bypass/listen ramps, poison reset, 64-sample chunking | `dsp/CompressorEngine.*`, `modes/Modes.def` | [D4] switch probe, [D5] block-size invariance |
| 12 | Telemetry: `UiFrame` seqlock (copy HR), history ring, tap | `dsp/Telemetry.h` | TSan clean; history continuity across block sizes |
| 13 | Analysis: `staticCurve`, `localRatio`, `stepPreview`, describing function | `analysis/*.h` | bit equality with the DSP (§6.5) |
| 14 | `OptoCell`, FET gain law + `FetColor`, `VarimuColor`, `TcSelector`, `SharedElementMax`, sliding-max limiter | `dsp/stages/…` | per-Mode goldens |
| 15 | Tools: `DspBench` (ns per Mode), `CurveProbe`, `SwitchProbe`, later `ModeFit` | `Tools/` | – |

**Interfaces to freeze before parallel work starts:**
- `simd` op names;
- `Pid`, `ParamSpec`, `Resolved`;
- the stage concept (`Coeffs`/`State`/`tick`/`seed`);
- `IEngine`, `Carry`, `ControlIo`, `UiFrame`.

After that, D §6.3's six-way split maps onto rows 3, 4+7, 5+6, 8+9, 10+14 colour, and 10+11 latency.

### 9.3 First Modes, to exercise the engine (D §6.3 order)

- **Clean:** `PeakLog`/`QuadKnee`/`SmoothBranching`, all continuous. The golden baseline.
- **Bus G:** stepped lists with an Auto detent and `DualRelease`.
- **FET 76:** input remap, ALL step, `FeedbackZdf`, FET colour, needs Std OS.
- **Opto 2A:** Remapped `thr`, `OptoCell`, emphasis SC, naive-FB-safe.
- **Mu 67:** `TcSelector`, `ProgressiveKnee` in ZDF, Lat/Vert.
- **Diode 609:** `SharedElementMax` Stage 2.
- **Bus 25:** FF/FB kernel switch through the crossfade, RMS, tilt, CV-sum link.
- **Brickwall:** sliding max + box, true peak, fixes the latency architecture.

---

## 10. Risks and open questions

1. **FET all-buttons curve and release law** are undocumented (D §8.1). The engine supports a custom-curve step. The
   constants need captures and `ModeFit`.
2. **Opto memory constants** are [H]. Their shape comes from the manual's "60 ms for 50 %, 1–15 s" and the LDR
   physics. Fit them to renders or captures before claiming fidelity.
3. **The SSL auto-knee law and Auto-release constants** are [H] (D §8.3). The SSB dataset (arXiv 2504.04589) can fit
   both.
4. **x86 `min`/`max` NaN semantics** differ from NEON. They are harmless only because the poison check and the floors
   keep NaN out; this needs a unit test.
5. **Host behaviour with `parameterInfoChanged`** varies. The text refresh on a Mode change may lag in some hosts.
   Only the display is affected, never the audio.
6. **Mix above 100 % on digital Modes** (D §4) needs the mix in the OS domain to support `wet·m + dry·(1−m)` with
   m > 1. That is fine, but it must be clamped for Unused-mix Modes.
7. **Quality changes latency.** Document it and make it non-automatable. A session saved at HQ reopens at HQ.

---

## 11. Deltas and agreements with the sibling reports

- **Agrees:**
  - B §7.4.1, 7.4.3–7.4.7: universal set, Int Mode slot, `modeId` in state, constant latency, key bus.
  - D §0.2 and §5.7: slope-domain ratio and `k = S/(1−S)`; independently derived here as `R_eff = 1 + k` (§2.6).
  - D §4 IDs; C §5.1 contract; C §5.5–5.6 (switch and zipper probes).
- **Changes:**
  1. B §7.4.2: *do not* rewrite raw values on a Mode change; snap on read (§4.2.1).
  2. C §5.1: `staticGainDb` wraps the SIMD kernel instead of a double re-implementation (§6.1).
  3. B §7.5: history is min/max *columns* at 1 kHz, not points.
  4. B §7.4.3: 128 Mode slots, not 64.
  5. B §7.4.5 says "maximum lookahead or oversampling delay". This report makes latency a function of the *global*
     Quality and Lookahead-enable only, never of Modes (§5.2).
  6. HR's block-rate smoothing is replaced by per-sample smoothing with 16-sample absolute ticks, to satisfy
     C §5.6.3.
- **New here:**
  - the naive-FB instability bound and the ZDF closed forms (§2.6);
  - the dbx-160-is-an-RMS-detector derivation (§2.5c);
  - measured JUCE OS latencies and costs (§2.9);
  - measured kernel costs (§3.7);
  - mixing inside the OS domain (§5.3);
  - the ADAA residual trick (§2.9).

---

## 12. Reproducing the [M] numbers

The scratch programs live in the session scratchpad
(`/private/tmp/claude-501/-Users-seanfunk-audio-plugins-FCompressor/1e18d990-0133-428b-8995-1664ebc8733a/scratchpad/`),
not in the repo:

| Program | What it measures | Build |
|---|---|---|
| `bench/kbench.cpp` | FF kernel: scalar libm, scalar poly, NEON 2-lane, NEON 4-lane; poly accuracy scan | `clang++ -std=c++17 -O3 -mcpu=apple-m1 -fno-math-errno -fno-trapping-math` |
| `oslat/main.cpp` | JUCE `dsp::Oversampling` latency (fractional and integer) and up+down cost for FIR/IIR × normal/max × 2/4/8 | JUCE modules compiled one TU each from `HardwareReverb/build/_deps/juce-src/modules`, read-only use |
| `verify.py` | dbx attack/release table; naive vs ZDF FB simulation | – |
| `zdfknee.py` | closed-form ZDF quadratic knee vs 200-step bisection, 2·10⁵ random cases | – |

All ran on this Mac (Apple Silicon); times are wall-clock over 20 s of stereo audio at 48 kHz. **Promote kbench and
oslat into `Tools/DspBench` rows when the engine exists.** Nothing was built or run inside HardwareReverb's build
directories.

---

## 13. Sources

- D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis," JAES
  60(6), 2012 (equations as reproduced in the MathWorks docs and CTAGDRC; ResearchGate record
  https://www.researchgate.net/publication/277772168).
- D. Giannoulis, M. Massberg, J. D. Reiss, "Parameter Automation in a Dynamic Range Compressor," JAES 2013
  (https://www.researchgate.net/publication/290088207), as implemented in CTAGDRC (https://github.com/p-hlp/CTAGDRC).
- MathWorks, `compressor` System object algorithm (knee, log(9) time constants, auto makeup)
  (https://www.mathworks.com/help/audio/ref/compressor-system-object.html).
- F. Eichas, U. Zölzer, "Modeling of an optocoupler-based audio dynamic range control circuit," Proc. SPIE 9948, 2016
  (https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Eichas-Modeling-of-an-optocoupler-based-audio-dynamic-range-control-circuit-99480W.pdf).
- E. Gerat, F. Eichas, U. Zölzer, "Virtual Analog Modeling of a UREI 1176LN Dynamic Range Control System," AES 143,
  2017 (https://aes2.org/publications/elibrary-page/?id=19249). F. Eichas et al., "Virtual Analog Modeling of Dynamic
  Range Compression Systems," AES 142, 2017.
- A. Wright, V. Välimäki, "Grey-Box Modelling of Dynamic Range Compression," DAFx20in22
  (https://www.dafx.de/paper-archive/details.php?id=Oz5HVaU0tKAjfD3ivOlv5g, code https://github.com/Alec-Wright/GreyBoxDRC).
- C. Steinmetz, J. Reiss, "Efficient neural networks for real-time modeling of analog dynamic range compression,"
  arXiv 2102.06200.
- "Sound Matching an Analogue Levelling Amplifier Using the Newton–Raphson Method," arXiv 2509.10706 (2025).
- "Solid State Bus-Comp: A Large-Scale and Diverse Dataset for Dynamic Range Compressor Virtual Analog Modeling,"
  arXiv 2504.04589 (2025).
- "Evaluating Dynamic Range Compressor Models Using Control-Voltage Measurements: an Approach and Dataset,"
  arXiv 2606.18573 (2026).
- J. Parker, S. D'Angelo, "A Digital Model of the Buchla Lowpass-Gate," DAFx-13 (vactrol model)
  (https://dafx.de/paper-archive/2013/papers/44.dafx2013_submission_56.pdf).
- J. Parker, V. Zavalishin, E. Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time
  Convolution," DAFx-16 (https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf).
- API, 2500+ Stereo Bus Compressor Operator's Manual (https://apiaudio.com/docs/manuals/2500+_user_23-01-09.pdf).
- Sound On Sound, Fairchild 660 & 670 review (https://www.soundonsound.com/reviews/fairchild-660-670). SOS API 2500
  review (https://www.soundonsound.com/reviews/api-2500).
- dbx 160X manual (https://www.technicalaudio.com/pdf/dbx/dbx_160x.pdf); dbx 160A
  (https://dbxpro.com/en-US/products/160a). Attack and release figures via vintagedigital.com.au.
- 1176 Peak Limiter, Wikipedia (https://en.wikipedia.org/wiki/1176_Peak_Limiter). Teletronix LA-2A figures via
  Vintage King (https://vintageking.com/teletronix-la-2a-optical-compressor-limiter).
- SSL G-series times (SOS XLogic G review; Waves SSL G-Master manual). SSL auto-release discussion, KVR
  (https://www.kvraudio.com/forum/viewtopic.php?t=537769).
- U. Zölzer (ed.), DAFX, 2nd ed. (2011), dynamics chapter. W. Pirkle, *Designing Audio Effect Plugins in C++*, 2nd ed.
  (2019), AudioDetector and dynamics chapters. J. Reiss, A. McPherson, *Audio Effects: Theory, Implementation and
  Application* (2014), dynamics chapter. V. Zavalishin, *The Art of VA Filter Design* (TPT SVF). These are background;
  the specific equations used here are cross-checked against the sources above.
- JUCE 8.0.4 source at `HardwareReverb/build/_deps/juce-src/modules`:
  - `juce_dsp/processors/juce_Oversampling.h:66-126`, `.cpp:564-590`
  - `juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp:857-882, 1365-1400`
  - `juce_audio_plugin_client_AU_1.mm:1946-1989`
  - `juce_audio_processors/processors/juce_AudioProcessor.h:843-850`
  - `juce_core/maths/juce_NormalisableRange.h:104-106`
  - `juce_audio_processors/utilities/juce_AudioProcessorParameterWithID.h:96`
