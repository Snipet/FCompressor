# D: compressor Mode catalogue (FCompressor)

Research for FCompressor's **Modes**: which hardware units and digital styles to model, what each one exposes, a
universal parameter superset that one shared UI can show, the **Mode × Parameter matrix** (the core artefact), names
that avoid trademarks, and an implementation order.

Companion documents: `A-gui-stack.md` (GUI) and `B-plugin-build.md` (parameters and build). This document follows B §7.4:
one stable superset of host parameters, with a per-Mode quantiser in a `ModeDescriptor`. It adds two parameter kinds that
B does not have, **remapped** and **extension** (§5.1), and it adds conditional step lists (§5.3).

Dates: research done 2026-09-22. The only HardwareReverb files read were `Source/PluginProcessor.cpp:65-120` (APVTS
layout, `AudioParameterChoice` for Matrix Order, percent attributes). Nothing in HardwareReverb was modified.

---

## 0. Decision-relevant summary

1. **Four ways a unit exposes "threshold".** A plugin needs all four:
   - A real threshold (SSL, API, dbx, Neve, CL 1B, VCA units).
   - Input gain into a fixed threshold: 1176, Distressor, TG1 "Hold", Sta-Level, GML in Hard-knee.
   - Side-chain gain: LA-2A/LA-3A Peak Reduction, Fairchild AC Threshold.
   - A threshold coupled to ratio: on the Altec 436C the ratio is 2:1 at a 0 dBm threshold and 4:1 at +16 dBm, and on the
     1176 higher ratios raise the threshold.

   The superset therefore keeps **Input** and **Threshold** as separate slots. A per-Mode *remap* ghosts one of them.
2. **Ratio is not always a number.** A unit may have:
   - Fixed lists: SSL 2/4/10, API 2500 1.5…∞, Neve 1.5…6, Distressor 1…Nuke.
   - Continuous ratio: CL 1B 2–10, 527 1–∞, E-channel 1–∞.
   - Named modes: LA-2A Compress/Limit, Manley Compress 1.5:1 / Limit 4→20:1, 525 2:1/20:1.
   - A level-dependent ratio: vari-mu, VSC-2 "Soft", GML "Soft".
   - Negative ratios: dbx 160X "∞+" down to −1:1, mpressor, elysia alpha in FF.

   Use a **slope domain** `s = 1 − 1/R` (0 = 1:1, 1 = ∞:1, 2 = −1:1) as the host representation. It makes
   continuous, stepped and negative ratios one float, and it is the same `slope` term the GlueCompressor gain computer
   already uses (B §6.1).
3. **Time controls come in six shapes:**
   - Continuous time.
   - Fixed time lists, some with an "Auto" position: SSL, VSC-2, Neve A1/A2.
   - Coupled attack+release presets: the Fairchild TC 1–6.
   - Rates in dB/ms and dB/s: dbx 165A.
   - Fully program-dependent with no control: LA-2A, LA-3A, dbx 160.
   - A macro: the GML "Timing" and "Release Hysteresis", and the CL 1B "Fix/Man", where Attack becomes a delay before the
     manual release.

   The superset needs **Attack, Release and a per-Mode "Time Mode" list**, and step lists that can **depend on another
   parameter**. The TG1 release list changes with Comp/Limit.
4. **Two-stage units are common:**
   - Neve 33609/2254: a compressor and a limiter feeding one diode bridge.
   - Shadow Hills: opto then VCA.
   - dbx 165A: compressor plus PeakStop.
   - elysia alpha: compressor plus soft clip.
   - Red 3: compressor plus limiter.

   Add a small **Stage 2** group (Threshold/Ceiling, Attack list, Release list). It is ghosted for most Modes.
5. **Topology matters for the static curve.** In a feedback (FB) design with a gain-reduction slope `k` per dB of
   *output* over the threshold, the effective ratio is `1 + k`. In feed-forward (FF) with slope `s` it is `1/(1−s)`. FB
   cannot reach ∞:1, and on the elysia alpha "the printed [ratio] values double in feed forward mode" [V S24]. The
   characteristics screen must plot FB and program-dependent Modes by **solving or simulating** the engine, not by
   evaluating an FF formula.
6. **Recommended first 8 Modes:** Clean (digital FF), Bus G (SSL G), FET 76 (1176), Opto 2A (LA-2A), Mu 67
   (Fairchild 670), Diode 609 (Neve 33609), Bus 25 (API 2500), Brickwall (look-ahead limiter). Together they build
   every engine feature except tempo-sync, negative ratios and the Distressor harmonic generator (§7).

---

## 1. Verification legend and sources

Each fact carries a tag:
- **[V Sx]**: verified from source Sx (manual, datasheet or manufacturer page read directly).
- **[V~ Sx]**: verified only through a search-engine summary or a secondary retailer page. Moderate confidence.
- **[C]**: sources conflict. Both values are given.
- **[U]**: unverified. It comes from my own knowledge or inference and must be checked before it is treated as exact.

| # | Source |
|---|---|
| S1 | Mix, "1176 Revision History" — https://www.mixonline.com/recording/1176-revision-history-372048 |
| S2 | UA 1176LN hardware manual (PDF, text extracted) — https://media.uaudio.com/assetlibrary/1/1/1176ln_manual.pdf |
| S3 | Sound On Sound, Fairchild 660 & 670 — https://www.soundonsound.com/reviews/fairchild-660-670 |
| S4 | Sound On Sound, Heritage Audio Herchild 670 — https://www.soundonsound.com/reviews/heritage-audio-herchild-670 |
| S5 | SSL G bus: UA SSL 4000 G Bus manual (search summary) + Waves SSL G-Master manual PDF — https://help.uaudio.com/hc/en-us/articles/30847649785748 ; https://assets.wavescdn.com/pdf/plugins/ssl-g-master-buss-compressor.pdf |
| S6 | API 2500+ Operator's Manual (PDF, text extracted) — https://apiaudio.com/docs/manuals/2500+_user_23-01-09.pdf |
| S7 | Sound On Sound, API 2500 — https://www.soundonsound.com/reviews/api-2500 |
| S8 | API 527A Operator's Manual (PDF, text extracted) — https://apiaudio.com/docs/manuals/527A_user_20-07-13.pdf ; product page https://apiaudio.com/product/527-compressor-limiter/ |
| S9 | API 525 manual (PDF) — https://www.barryrudolph.com/recall/manuals/br525.pdf ; product page https://apiaudio.com/product/525-compressor/ |
| S10 | Neve 33609/N owner's manual (archive.org text + AMS Neve PDF) — https://archive.org/stream/Neve_33609-N_owners_manual/Neve_33609-N_owners_manual_djvu.txt ; https://www.ams-neve.com/wp-content/uploads/2022/01/33609N1.0usermanual.pdf |
| S11 | Sound On Sound, Neve 2254/R — https://www.soundonsound.com/reviews/neve-2254r |
| S12 | Manley Variable Mu product page + owner's manual (PDF; its text uses a substitution-encoded font and was decoded) — https://www.manley.com/products/pro-audio/dynamics/variable-mu ; https://static1.squarespace.com/static/582f5ce4e4fcb562b921bdc3/t/606b47fdfadd0b24f0d2b045/1617643525935/VARIABLE+MU+MANUAL+WEB+REV+5.2.1.pdf |
| S13 | Softube Tube-Tech CL 1B manual + UA CL 1B Mk II PDF — https://www.softube.com/us/user-manuals/tube-tech-cl-1b-and-cl-1b-mk-ii-compressors |
| S14 | Empirical Labs Distressor manual (PDF, text extracted) + product page — https://www.empiricallabs.com/wp-content/uploads/distressor_manual.pdf ; https://www.empiricallabs.com/distressor/ |
| S15 | Sound On Sound, Distressor — https://www.soundonsound.com/reviews/empirical-labs-distressor |
| S16 | Mix (Barry Rudolph), Chandler TG1 — https://www.barryrudolph.com/mix/chandler.html |
| S17 | Softube Chandler Zener Limiter manual — https://www.softube.com/user-manuals/chandler-limited-zener-limiter |
| S18 | Plugin Alliance Shadow Hills manual (PDF) + Mix review — https://files.plugin-alliance.com/products/shadow_hills_mastering_compressor/shadow_hills_mastering_compressor_manual.pdf ; https://www.mixonline.com/technology/reviews/review-shadow-hills-mastering-compressor |
| S19 | Sound On Sound, Smart Research C2 — https://www.soundonsound.com/reviews/smart-research-c2 |
| S20 | Sound On Sound, Vertigo VSC-2 — https://www.soundonsound.com/reviews/vertigo-vsc2 |
| S21 | Sound On Sound, Retro Instruments Sta-Level — https://www.soundonsound.com/reviews/retro-instruments-sta-level |
| S22 | Altec 436C datasheet (PDF, text extracted) — https://www.barryrudolph.com/recall/manuals/altec_436c.pdf |
| S23 | GML 8900 User's Reference (PDF) — https://www.massenburg.com/wp-content/uploads/2020/02/8900-Ref-Guide.pdf |
| S24 | elysia alpha compressor V2 plugin manual (PDF) + Mix field test — https://files.plugin-alliance.com/products/elysia_alpha_compressor_v2/elysia_alpha_compressor_v2_manual.pdf ; https://www.mixonline.com/technology/field-test-elysia-alpha-compressor-369697 |
| S25 | Sound On Sound, elysia mpressor — https://www.soundonsound.com/reviews/elysia-mpressor |
| S26 | Waves SSL E-Channel & G-Channel manual (PDF) — https://assets.wavescdn.com/pdf/plugins/ssl-e-channel.pdf |
| S27 | SSL Channel Strip Guide + SSL E-Series Dynamics Module page — https://www.solidstatelogic.com/channel-strip-guide ; https://solidstatelogic.com/products/e-series-dynamics-module |
| S28 | dbx 160: historyofrecording.com, SOS Waves dbx 160 review, vintagedigital.com.au (search summary) — https://www.historyofrecording.com/dbx_Model_160_and_161.html ; https://www.soundonsound.com/reviews/waves-dbx-160 |
| S29 | dbx 165A: search summary of vintagedigital.com.au / manualzz owner's manual — https://manualzz.com/doc/6583607/dbx-165a-owner-s-manual |
| S30 | FabFilter Pro-C 2 manual (PDF) + Pro-C 3 help — https://www.fabfilter.com/downloads/pdf/help/ffproc2-manual.pdf ; https://www.fabfilter.com/help/pro-c/using/styleandcharacter ; …/dynamicscontrols ; …/timecontrols |
| S31 | LA-2A: UA tips page, mix:analog LA-2A article — https://www.uaudio.com/blogs/ua/la-2a-collection-tips-tricks ; https://blog.mixanalog.com/dual-la2a-compressor |
| S32 | LA-3A: UA manual (search summary), Mix field test — https://media.uaudio.com/assetlibrary/l/a/la-3a-manual.pdf ; https://www.mixonline.com/technology/field-test-universal-audio-la-3a-audio-leveler-370271 |
| S33 | Pye 4060: Vintage King listing (search summary) — https://vintageking.com/pye-4060-mono-compressor-limiter-291-used |
| S34 | Focusrite Red 3: search summary (zzounds / Focusrite) — https://www.zzounds.com/item--FOCRED3 |
| S35 | Gates Level-Devil: Reverb listing (search summary) — https://reverb.com/item/84336324-gates-level-devil-m-5546-tube-compressor-limiter-c-1950-varimu-original-vintage-usa |
| S36 | Pulsar Audio, "History of all-buttons-in" — https://pulsar.audio/blog/the-history-of-all-buttons-in-mode/ |
| S37 | JustMastering classic compressor guide — https://www.justmastering.com/article-classiccompressorsguide.php |

The UA help-centre pages returned HTTP 403. The Focusrite Red 3 brochure and the elysia hardware manuals are
image-only or encrypted PDFs, and no text could be extracted from them.

---

## 2. Unit catalogue

Each entry covers topology, controls (exact), static curve, ballistics and coloration. "dBu" thresholds become dBFS
through a global reference level. Suggested default: +4 dBu = 0 VU = −18 dBFS, so 0 dBFS = +22 dBu. This is the
convention Waves' SSL plug-ins use ("Meters are calibrated to 18 dBu dBFS" [V S5]).

### 2.1 FET

#### UREI/UA 1176 (rev A … H, LN)
- **Topology.** The FET works as a voltage-controlled resistor, the lower leg of a divider across the signal. It is a
  **feedback** design: "the sidechain circuit samples the signal level after the gain reduction" [V S2]. The detector is
  peak [U: the wording is standard, but the manual only says "peak limiter" in the title]. Stereo uses the 1176SA
  adapter. When linked, "the fastest attack time is doubled … 40 microseconds instead of 20", and each unit's
  Attack/Release knobs affect both [V S2].
- **Controls** [V S2]:
  - **Input**: sets the threshold *and* the drive. "In the 1176LN, the Input knob controls both the threshold and the
    amount of input signal". Unity is about 24 on both Input and Output at 12 o'clock.
  - **Output**: output level.
  - **Attack**: 20 µs … 800 µs. **Fastest fully clockwise.** The fully counter-clockwise position is **OFF**, which
    disables gain reduction while the audio still passes the transformers.
  - **Release**: 50 ms … 1100 ms. **Fastest fully clockwise.**
  - **Ratio** buttons: 4, 8, 12, 20. "Higher Ratio settings also set the threshold higher."
  - **Meter**: GR / +8 / +4 / Off.
- **All-buttons ("British") mode.** "Distortion increases radically due to a lag time on the attack". The curve shifts
  continually [V S2]. Ratio is "between 12:1 and 20:1", and the bias points shift across the circuit [V~ S36]. Pressing
  4 and 20 together gives a similar result [V~ S36].
- **Static curve.** Fixed-ratio buttons. The threshold rises with ratio [V S2]. Knee shape: 4:1 is soft, 20:1 is
  close to hard [U]. All-buttons gives a plateau with overshoot, a "reverse look-ahead" [V~ S36].
- **Ballistics.** Very fast attack. The release is program-dependent within the knob range [U: magnitude].
- **Coloration.** Distortion is "within 0.5 % THD 50 Hz–15 kHz with limiting, at 1.1 s release". The comparison symbol
  was lost in extraction, so read it as the spec bound [V S2]. Distortion rises with fast release and low frequencies.
  Gain is 45 dB ±1 [V S2]. The LN circuit (rev C onward) "was designed to reduce the distortion that the FET introduced"
  [V S2].
- **Revisions** [V S1]:

  | Rev | Serials | Change |
  |---|---|---|
  | A | 101–125, 1967 | Blue stripe. FET preamp. Class-A output with UA-5002 transformer. No LN, so more THD and noise. |
  | AB | 125–216 | Preamp resistor changes and bypass caps for stability and noise. |
  | B | 217–1078 | All-bipolar preamp. |
  | C | 1079–1238, 1970 | Black face. LN circuit (Plunkett) lowers the FET drain-source voltage. |
  | D | 1239–2331 | LN on the main board. Q-bias trim. |
  | E | 2332–2611 | 110/220 V switchable mains. |
  | F | 2612–7052 | Push-pull output stage instead of Class A. Op-amp metering. |
  | G | 7053–7651 | Differential input instead of the input transformer. |
  | H | 7652+ | Silver panel. |

  The UA reissue is "patterned on … the D/E versions" [V S2].

### 2.2 Opto

#### Teletronix LA-2A
- **Topology.** A T4B optical attenuator (electroluminescent panel plus photoresistors). Opto detector with memory.
  Feedback [U]. Tube line amp and UTC transformers [V S31 mix:analog]. Tube types: 12AX7, 12BH7, 6AQ5/6P1 [U].
- **Controls.**
  - Gain (continuous).
  - Peak Reduction (continuous side-chain gain, so effectively the threshold).
  - Compress/Limit switch.
  - **R37 "Emphasis"**, a set-screw side-chain shelf. It gives "high-emphasis for the frequencies above 1 kHz" and can
    attenuate lows "for up to 10dB" [V S31].
  - Meter select [U].
  - Ranges are scale 0–100 [U].
- **Static curve.** Program- and frequency-dependent. In Compress it is roughly 3:1, and Limit moves it "closer to
  10:1"; "the exact curves of both modes are not available in the original manual" [V S31]. Other sources say 4:1 in
  Compress [C]. The knee is very soft.
- **Ballistics.** Attack is about 10 ms on average. Release is about 60 ms to 50 %, then 1–15 s for the rest,
  depending on the program's history [V~ S31]. Another source says 40–80 ms to 50 %, then up to 2 s [C].
- **Coloration.** Tube and transformer; a smooth, low-order signature [U].

#### UREI LA-3A
- **Topology.** A T4 opto cell in a solid-state class-A circuit with transformers [V S32]. Feedback [U].
- **Controls** [V S32]:
  - Gain and Peak Reduction (both continuous).
  - A rear Compress/Limit switch. The two "sound virtually indistinguishable unless very heavy compression is used".
  - A rear pot for side-chain HF sensitivity.
  - A −20 dB input pad (reissue).
  - GR/Output meter. Stereo link through the barrier strip.
- **Static curve.** Program-dependent. As a limiter it reaches about 50:1 [U, one search summary].
- **Ballistics.** Attack "1.5 ms or less, program dependent". Release stage 1 is 60 ms to 50 % [V~ S32]. After that,
  release is quick with occasional light compression (−3 dB) and slower when driven hard continuously (−7 to −10 dB)
  [V~ S32].
- **Coloration.** Class-A solid state with transformers. Brighter and faster than the LA-2A [U].

#### Tube-Tech CL 1B
- **Topology.** Optical gain element with a tube amplifier. FF/FB not documented [U]. An external side-chain bus links
  two units [V S13].
- **Controls** [V S13]:
  - Threshold +20 … −40 dB (plus Off).
  - Ratio 2:1 … 10:1, continuous.
  - Attack 0.5 … 300 ms.
  - Release 0.05 … 10 s.
  - Gain off … +30 dB.
  - **Attack/Release Select**:
    - **Fixed**: attack 1 ms, release 50 ms.
    - **Manual**: the knobs as normal.
    - **Fix./Man.**: fast fixed attack. After a short peak it releases fast. The **Attack knob becomes a delay
      before the manual Release takes over**, and this applies only when the peak is shorter than the Attack setting.
  - The Mk II plugin adds a side-chain low cut (frequency not stated) and parallel compression.
- **Static curve.** Opto plus a continuous ratio. Soft [U].
- **Coloration.** Tube (ECC82 is listed in the Tube-Tech spec sheet index [V~]) and transformer.

### 2.3 Variable-mu (tube)

#### Fairchild 660 (mono) / 670 (stereo)
- **Topology.** Variable-mu, using "four 6386 dual-triode valves" per channel in the gain stage. 20 valves, 11
  transformers, about 30 kg [V S3]. **Feedback**: the API manual groups "660 type compressors" with feedback designs
  [V S8].
- **Controls.**
  - **Input**: a 20 dB attenuator. SOS says "a rotary switch increases the input signal in 1dB steps" [V S3]. The
    Herchild review says a "20dB attenuator" scaled 20→0 [V S4].
  - **AC Threshold**: 0–10, where 10 (fully CW) means no compression [V S4]. This is side-chain gain.
  - **DC Threshold** (bias): fully anticlockwise gives hard-knee peak limiting, fully clockwise a gentle ratio and knee
    [V S4]. In practice it is a **knee/ratio control**.
  - **Time Constant**: positions 1–6.
  - **AGC switch** (4 positions): Independent / Link / **Lat-Vert** (M/S), plus linked M/S on the Herchild [V S4].
  - Meter Bal/Zero [V S4].
  - The original has no output/makeup control [U].
- **Time constants.** Attack is "between 0.2 and 0.8 milliseconds" [V S3].

  | TC | Attack | Release |
  |---|---|---|
  | 1 | 0.2 ms | 0.3 s |
  | 2 | 0.2 ms | 0.8 s |
  | 3 | 0.4 ms | 2 s |
  | 4 | 0.8 ms [V S3] / 0.4 ms [V S4] **[C]** | 5 s |
  | 5 | 0.4 ms | program: 2 s for individual peaks, 10 s for multiple peaks |
  | 6 | 0.2 ms | program: 0.3 s peaks, 10 s multiple peaks, 25 s for consistently high program |

- **Static curve.** "Gain reduction starts with a very low ratio, between 1:1 and 2:1 for smaller peaks, and gradually
  increases to a ratio of up to 20:1" [V S3]. JustMastering says 2:1 to 30:1 [C S37]. The curve is intrinsically soft,
  and the DC threshold reshapes it.
- **Coloration.** Balanced push-pull triodes cancel even harmonics, so the remaining distortion is mostly odd and rises
  with GR [U]. The transformers add LF weight [U].

#### Manley Variable Mu (Stereo Limiter Compressor)
- **Topology.** Remote cut-off tube (5670 as standard; the T-Bar mod uses 6BA6/12BA6, originally the 6386). "The gain
  control chain is technically called a feedback circuit" [V S12 manual]. Link "combine[s] the 'DC' control signals not
  the audio" [V S12 manual]. The Mastering and M/S versions add M/S encode and decode [V S12 manual].
- **Controls** [V S12]:
  - Input (continuous).
  - Threshold (continuous).
  - Attack 25–70 ms continuous. The manual marks Fast ~25, Med ~50, Slow ~70 ms.
  - **Recovery 5 steps: 0.2 s, 0.4 s, 0.6 s, 4 s, 8 s.** The decoded manual text lists "8 s / 1(?) s / 0.6 s /
    0.1(?) s". The digits did not decode cleanly, so use the manufacturer page.
  - **Compress (1.5:1, soft knee) / Limit (4:1, rising to 20:1)**. "At greater than 12 dB of limiting the ratio
    increases (up to 20:1)" [V S12 manual].
  - Output attenuator.
  - Link: Sep/Link.
  - **HP side-chain: −3 dB @ 100 Hz**.
  - Bypass.
- **Mastering version** [V S12 manual]:
  - Threshold has 21 steps, "calibrated to LIMIT mode", about a 12 dB range in Limit and about 6 dB in Compress.
  - The attack range is "slightly extended in both directions" and has 11 positions.
  - Output attenuators move in 0.5 dB steps. Reference unity is −11.5.
- **Static curve.** Soft, and "the 'knee' softens as more limiting is used … like a compressor followed by a limiter"
  [V S12 manual].
- **Coloration.** Under 0.1 % THD @ 1 kHz [V S12]. Pushing Input and pulling Output gives "gentle tube distortion"
  [V S12 manual]. The 6BA6 sounds "less squashed" than the 5670 at heavy limiting [V S12 manual].

#### Gates Sta-Level (and Level-Devil)
- **Topology.** Variable-mu feedback. 12AX7 or 6386 control valve, 12AT7, a 6V6 push-pull output, a 6AL5 side-chain
  rectifier and an OB2 regulator (Retro reissue) [V S21]. Linkable for stereo [V S21].
- **Controls.**
  - Input and Output.
  - A **fixed threshold**, so Input sets the compression.
  - Recovery: the original has a Single/Double toggle [V~ S21 search]. The Retro version adds Triple and a 6-position
    release [V S21].
  - Mode behaviour [V S21]: Single "expands both time constants for a very slow response". Double is closest to the
    original. Triple has a faster attack and a more responsive release.
- **Static curve.** "Around 3:1" with a soft knee and up to 40 dB of GR [V S21].
- **Ballistics.** Program-dependent. Release is faster at higher GR because the 6386 transconductance changes [V~ S21
  search].
- **Level-Devil.** Up to 25 dB of GR. **An internal switch adds expansion of quiet signals by up to 10 dB.** Fast
  attack, very slow release [V~ S35]. The expander is **outside the superset** (§4 "Dropped").

#### Altec 436 (A/B/C)
- **Topology.** Variable-mu 6BC8 with a 6CG7 and a 6AL5 rectifier [V S22]. The 436A has no controls, the 436B adds
  input gain, and the 436C adds threshold and release [V~].
- **Controls (436C)** [V S22]:
  - Gain.
  - **Threshold: 0 to +16 dBm output.**
  - **Release: 0.3–1.3 s (63 % recovery).**
  - **Attack fixed at 50 ms.**
- **Static curve.** "**2:1 at 0 dbm threshold; 4:1 at +16 dbm threshold**" [V S22]. Threshold and ratio are coupled.
  Maximum compression is 30 dB [V S22].
- **Coloration.** Under 1.5 % THD at 25 dB of GR (35 Hz–15 kHz), under 2.5 % at 30 dB [V S22].

### 2.4 VCA and discrete feed-forward

#### SSL G-series bus compressor
- **Topology.** VCA, feed-forward [U, but widely documented]. Stereo sums to one detector, always linked [U].
- **Controls** [V~ S5]:
  - Threshold −15 … +15 dB. Some console versions are marked ±20 [C/U].
  - Makeup −5 … +15 dB.
  - **Ratio 2, 4, 10.**
  - **Attack 0.1, 0.3, 1, 3, 10, 30 ms.**
  - **Release 0.1, 0.3, 0.6, 1.2 s, Auto.** In Auto the release "is dependent upon the duration of the program peak".
- **Waves plug-in extras** [V S5]: Mix 0–100 % (0.1 % steps), Trim −18 … +18 dB, Autofade 1–60 s, "Analog" toggle.
- **Static curve.** Fixed ratio and a fixed knee (moderately soft) [U].
- **Ballistics.** Auto is a two-time-constant release. GlueCompressor's seed uses a 50 ms fast stage and a 600 ms slow
  stage (B §6.1) [U as an emulation of SSL].
- **Coloration.** Low. VCA plus console bus summing. The original has no side-chain HPF [U]. Later SSL units add one [U].

#### SSL E/G channel dynamics (4000E/G; 9000J/K for reference)
- **Controls** [V S26]:
  - Compressor: ratio 1 … ∞ (continuous), threshold +10 … −20 dB, attack auto (program-sensing) or **F.ATK = 1 ms**,
    release 0.1–4 s, automatic makeup ("calculated from the Ratio and Threshold settings"), soft knee.
  - Expander/gate: threshold −30 … +10 dB, range 0–40 dB, release 0.1–4 s, F.ATK, gate switch.
- **E-series specifics** [V S27]:
  - Auto attack is "adjustable between 3ms and 30ms" (program-dependent).
  - Revision 4 has a logarithmic release and a soft knee.
  - The modern E module has switches "to defeat the over-easy curve and to use a linear release".
  - The side chain is "true RMS" (module page). The strip guide says "peak sensing with soft-knee option" **[C]**.
  - The 9000 J/K has a **Peak vs RMS** switch [V S27].
  - The E routes its whole EQ into the side chain; the G routes only its HPF/LPF [V~ S27].
- **Waves filters** [V S26]: HPF 18 dB/oct, LPF 12 dB/oct. Either can be placed before the dynamics ("Split").

#### API 2500 / 2500+
- **Topology.** VCA (THAT 2180) with a 2252 RMS detector [V S7] and 2510/2520 discrete op-amps with an output
  transformer [V S6]. **TYPE: New = feed-forward, Old = feedback** [V S6]. On link, "the control voltages are summed
  together, but both channels are still detecting their own control voltages" [V S6].
- **Controls** [V S6]:
  - Threshold: +20 … −20 dB on the 2500+. The original 2500 is +10 … −20 dBu [V S7].
  - **Attack .03, .1, .3, 1, 3, 10, 30 ms.**
  - **Ratio 1.5, 2, 3, 4, 6, 10, ∞.**
  - **Release .05, .1, .2, .5, 1, 2 s + Variable (50 ms … 3 s).**
  - **Knee Hard / Med / Soft.**
  - **Thrust Norm / Med / Loud**:
    - Loud is a "gradual, 10dB/decade" filter (≈3 dB/oct, the inverse pink curve).
    - Med gives "slight low frequency attenuation and a slight high frequency boost".
    - SOS describes Med as 2 dB/oct and Loud as 4 dB/oct **[C S7]**.
  - **L/R Link: IND, 50, 60, 70, 80, 90, 100 %.**
  - **Shape** (2500+): Fast (peak) / Slow (RMS) / Both. The original 2500 offers link filters flat/HP/LP/BP [V S7].
  - Makeup Auto or Manual 0 … +24 dB.
  - **Blend**: 100 % / Crossfade (wet/dry) / Parallel (input plus output), with a Mix %.
- **Static curve.** Three knee shapes. FF follows the ratio exactly. FB is "smoother, softer" [V S6].

#### API 525 (feedback, early 1970s; reissue)
- **Topology.** "Peak detecting feedback compressor/limiter" [V S9].
- **Controls** [V S9]:
  - Input (threshold) and Output.
  - **Ceiling**, which moves threshold and output together.
  - **Comp 2:1 / Limit 20:1** switches.
  - OFF (GR off, unit in line).
  - **Release: 0.1, 0.5, 2.0, 2.5 s** (manual: "All out = 0.1 sec, In = .05 … 2.0 … 2.5"). The product page says
    0.1/0.5/1.5/2.0 s **[C]**.
  - **De-Ess**: "inversion of the voice energy curve" in the detector.
  - Bypass. Link: two or more 525s.
- **Ballistics.** **Attack 15 µs fixed.** "Release times vary with frequency, with high frequency/full bandwidth
  content released faster than just low frequency" [V S9].
- **Static.** Maximum GR 25 dB. THD 0.5 % max, 30 Hz–20 kHz [V S9].

#### API 527 / 527A
- **Controls** [V S8]:
  - Threshold +10 … −20 dB.
  - **Ratio 1:1 … ∞:1, continuous.**
  - **Attack 1 … 25 ms.**
  - **Release 0.3 … 3 s.**
  - Output −∞ … +10 dB. All continuous, each with **31 detents**.
  - **Type New (FF) / Old (FB)**.
  - **Knee Hard / Soft.**
  - **Thrust In/Out**: "down 15dB at 20Hz and up 15dB at 20kHz" into the RMS detector.
  - **Link**: a DC control-voltage sum bus, *not* master/slave.
- **Detector.** RMS [V S8].

#### dbx 160 (160/160X/160A family)
- **Topology.** True-RMS detector, discrete VCA, feed-forward [V S28].
- **Controls.**
  - Threshold, Compression (ratio), Output [V S28].
  - Ratio: "1:1 through to hard limiting". The later spec reads "variable 1:1 to ∞:1 thru to −1:1 with greater than 60 dB
    maximum compression" [V~ S28]. The negative part is on 160X-era models [U which models].
  - The original is hard-knee, and OverEasy came later [V S28 SOS: Waves models "the original hard-knee design"].
  - Ranges: threshold about −40 … +20 dB and output about ±20 dB [U].
- **Ballistics** (fixed, program-dependent) [V~ S28]:
  - Attack: 15 ms for a 10 dB step above threshold, 5 ms for 20 dB, 3 ms for 30 dB.
  - Release: about 8 ms for 1 dB, 80 ms for 10 dB, 400 ms for 50 dB (≈120–125 dB/s).

#### dbx 165A
- **Controls** [V~ S29]:
  - Threshold −40 … +20 dBV.
  - Ratio 1:1 … ∞:1.
  - **Attack/Release: Auto (program-dependent) or Manual.** Manual attack is a rate of 400 → 1 dB/ms, and manual
    release is 10 → 4000 dB/s.
  - OverEasy knee.
  - **PeakStop**, a post-output absolute limit adjustable from −2 to +24 dBm.
  - Separate detector input.
  - Stereo strap.
  - RMS meter with a 30 dB range.

#### Focusrite Red 3
- **Topology.** A "single-VCA design" handles both the compressor and the limiter. The discrete Focusrite VCA is
  balanced [V~ S34].
- **Controls** [V~ S34]:
  - Compressor: Ratio 1.5:1 … ∞:1 (continuous), Threshold, Attack, Release 0.1 s … 4 s, Auto release, Makeup.
  - Limiter: threshold.
  - Link.
  - The threshold range is reported as "−50 to −10 dB", which looks like a plug-in value [C/U]. The attack range is
    unknown [U].

#### GML 8900
- **Topology.** Log converters and three detectors (**Slow RMS, Fast RMS, Peak**) followed by a detector comparator.
  Discrete VCA, feed-forward [V S23].
- **Controls** [V S23]:
  - Threshold.
  - **Ratio**: a "Soft" position gives a soft knee where the ratio rises automatically. Otherwise it is hard-knee,
    **1.5:1 … ∞**.
  - **Timing**: couples the attack and release of both RMS detectors and sets the peak detector's release.
  - **Release Hysteresis**: an independent slow-RMS release.
  - **Crest Factor Fast** and **Crest Factor Peak**: the thresholds of those detectors relative to Slow RMS. A higher
    value preserves more transients. The Peak crest factor is internally 8 dB above Slow RMS [V~ search].
  - Output. Stereo Couple (control signals cross-combined; every detector stays active).
- **Mode behaviour.** In Hard-knee "the Threshold control should be thought of as a compression gain control … [it]
  acts as an input gain stage" (1176-like). In Soft it is a conventional threshold (LA-2A-like) [V S23].

#### Alan Smart C2 (Smart Research)
- **Controls.**
  - Threshold −20 … +20 dB [V S19].
  - Ratio 1.5, 2, 3, 4, 10, Limit [V~].
  - Attack in 7 steps, "very fast (~⅓ ms) to 30 ms" [V S19]. The retail list reads 0, 0.1, 0.3, 1, 3, 10, 30 ms [V~].
  - Release 0.1, 0.3, 0.6, 1.2, 2.4 s [V~]. SOS says "six steps, 100 ms to 2.4 s" with no Auto **[C: 5 vs 6]**.
  - Makeup up to +20 dB.
  - **Crush**: an extra FET gain-reduction stage with "progressive distortion" and a fixed brightening EQ.
  - Side-chain ext in. Link.
- **Topology.** THAT VCA differential pair, feed-forward [V S19].
- **SC HPF.** A 150 Hz −6 dB/oct high-pass on the input split, feeding the external SC input [V~].

#### Vertigo VSC-2
- **Topology.** Four discrete VCAs: one in the audio path and one in the side chain per channel. Feed-forward, since
  the detector taps "right after the input" [V S20].
- **Controls.**
  - Ratio **Soft (1:1 rising to 8:1), 2, 4, 8, 10, Brick (40:1)** [V S20]. Another retailer lists 2, 4, 8, 10, Brick
    plus Soft [V~].
  - Attack in 6 steps, 0.1 … 30 ms [V S20]. The intermediate values match SSL's 0.1/0.3/1/3/10/30 [U].
  - Release has 6 options from 100 ms to 1.2 s plus Auto [V S20]. The exact list is [U].
  - Threshold −22 … +22 dBu and makeup up to +22 [V~ MusicRadar].
  - A per-channel SC HPF (60 Hz and 90 Hz mentioned) [V~].
  - Stereo / dual mono. No external SC.

#### elysia alpha compressor
- **Topology.** Discrete class-A. **Feed-backward (default) or feed-forward per channel** [V S24]. "Ratio … The printed
  values double in feed forward mode" [V S24]. In FB the ratio tops out at about 2.5:1. FF allows limiting and negative
  ratios [V~ search].
- **Controls** [V S24]:
  - Thresh, Attack, **Auto Fast (attack)**, Release ("period … to return to unity gain after 6 dB of gain
    reduction"), **Auto Fast (release)**, Ratio. All are 21-detent pots [V elysia page].
  - **SC filter**: one knob sweeps LP → shelves → HP. The corner is 30–3300 Hz [V S24 Mix].
  - **Niveau** audio tilt filter, with 42 corners from 20 Hz to 20 kHz [V S24 Mix].
  - Parallel mix (Direct / Compressed / Mix).
  - Gain.
  - Warm/Transformer.
  - **Soft Clip** threshold. The Mix test used +21 dBu.
  - M/S mode. Channel Link.
- **Plugin extras** [V S24]: SC link %, GR Limit, Headroom, TX-Drive, Mono Maker 20–2000 Hz, Width, Auto gain.
- Numeric ranges for Attack, Release, Threshold and Ratio: **[U]**.

#### elysia mpressor
- **Topology.** Discrete class-A, feed-forward [V S25].
- **Controls** [V S25]:
  - Threshold, Attack, Release, Ratio (**negative ratios**).
  - **Anti-Log**: release is slower at the start and speeds up as the signal falls [V~].
  - **Auto-Fast.**
  - **Gain Reduction Limiter 0–21 dB**, which clamps the control voltage.
  - **Niveau filter 26 Hz–2.2 kHz (×10 switch)**.
  - Gain.
- Numeric ranges: **[U]**.

### 2.5 Diode bridge

#### Neve 33609 (/N)
- **Topology.** A diode-bridge gain element with separate compressor and limiter side chains. **Both are feedback**, per
  the 2254/R, which shares the lineage [V S11]. Stereo mode links the side chains internally: "both channels always …
  compressed by the same amount" [V S10].
- **Compressor** [V S10]:
  - Ratio 1.5:1 … 6:1 (list 1.5/2/3/4/6 [V~]). "Soft and progressive, so the true ratio is only attained >5dB above
    the threshold."
  - **Recovery 100 ms … 1500 ms** (list 100/400/800/1500 [V~]), plus **a1 = 100 ms/2000 ms** and **a2 = 50 ms/5000 ms**
    (self-adjusting).
  - **Attack Fast ~3 ms / Slow ~6 ms.** Slow adds "reduced sensitivity to <100Hz signals" (a 100 Hz first-order
    high-pass).
  - Threshold (dBu) and Gain. The steps are [U]; use the 2254/R's 2 dB steps from −20 to +10 dBu and 0 … +20 dB as the
    working assumption.
- **Limiter** [V S10]:
  - Threshold +4 … +15 dBu. Steps [U]: 1 dB.
  - Recovery 50 … 800 ms, plus a1/a2. List [U]: 50/100/200/800.
  - **Attack Fast ~2 ms / Slow ~4 ms.**
  - Ratio [U]: >100:1, per 2254 sources.
- **Coloration.** THD 0.075 % in bypass. The compression figure is garbled in the source. Under 0.45 % with the
  limiter in (1 kHz, 800 ms recovery). Maximum output +26 dBu [V S10].

#### Neve 2254 (/A, /E, /R)
- **Compressor** [V S11]:
  - **Threshold −20 … +10 dBu in 2 dB steps.**
  - **Ratio 1.5, 2, 3, 4, 6.** The knee is "fairly soft … over a 10dB range".
  - **Recovery 100, 200, 800 ms + Auto.**
  - **Makeup up to 20 dB in 2 dB steps.**
- **Limiter** [V S11]:
  - **Threshold +4 … +20 dBu in 2 dB steps.**
  - **Recovery 100, 200, 800 ms + Auto.**
- **Timing** [V S11]:
  - **Attack fixed 5 ms.** An optional fast attack is adjustable from 100 µs to 2 ms.
  - "In the Auto Recovery mode the attack time is adjusted automatically along with the release".
- **Structure** [V S11]: diode-bridge VCA. "Both stages … are feed-back designs". Four transformers, discrete class A.
  Linking: "outputs of both side-chains are combined before feeding the gain-control element".
- **Conflicts.** A secondary source lists compressor recovery 400/800/1500/Auto and a limiter ratio above 100:1
  **[C/V~]**.

### 2.6 Zener / transistor

#### EMI TG12413 → Chandler TG1 Limiter
- **Controls** [V S16]:
  - **Hold** (input and threshold in one knob).
  - **Comp/Limit**. **Ratio is fixed at 2:1 in both modes.**
  - **Attack: Comp 47 ms fixed, Limit 8 ms fixed.**
  - **Recovery, 6 positions:**
    - Limit: 0.05, 0.1, 0.25, 0.5, 1, 2 s.
    - Comp: 0.25, 0.5, 1.2, 2.5, 5, 10 s.
  - **Output: 21 positions, ±10 dB** (centre 0).
- **Coloration.** Zener gain element with "smooth, pleasing and colorful distortion". THD 0.22 % at full compression
  [V S16].

#### Chandler Zener Limiter (extended TG12413)
- **Controls** [V S17]:
  - **Comp 1** (2:1, slower, "RS124"-like), **Comp 2** (between), **Limit** (faster, "Fairchild 660 curves").
  - **Attack: 11 positions.** Position 2 is the original. Position 1 is 28 ms in Comp 1 and 5 ms in Limit [V~].
  - **Release: 21 positions.** Positions 1–6 are the original EMI six.
  - Input: 21 positions, with High/Low (12 dB).
  - Output: 21 positions, ±10 dB in 1 dB steps.
  - The plugin adds an SC filter at 30–300 Hz, Link and a THD toggle.

#### Pye 4060
- **Controls** [V~ S33]:
  - **Threshold −20 … +16 dB.**
  - **Ratio 1:1, 2:1, 3:1, 5:1, Limiting.**
  - **Decay 100, 200, 400, 800, 1600 ms.**
  - **No attack control.**
- **Topology.** Class-A transistor, originally germanium [V~]. Gain element [U].

### 2.7 Hybrids

#### Empirical Labs Distressor (EL8 / EL8-X)
- **Topology.** A digitally controlled analogue VCA with a distortion generator (manual block diagram: "VCA → Distortion
  Generator") [V S14]. Special detector circuitry is used for 2:1, 10:1 and Nuke [V S14].
- **Controls** [V S14]:
  - **Input 0–10**: drives into a fixed threshold. Input 0 = off.
  - **Output 0–10**. "Output at 8" is about +4 dBu tape level.
  - **Attack 0–10 = 50 µs … 30 ms.** The website says 50 ms **[C]**. 0 is fastest ("If the attacks are too fast
    (towards 0)").
  - **Release 0–10 = 0.05 … 3.5 s, and up to 20 s in 10:1 Opto.**
  - **Ratio 1, 2, 3, 4, 6, 10 (Opto), 20, Nuke.**
  - **Detector button, 8 states:** Norm / HP / Band-Emphasis / HP+BE / Link / Link+HP / Link+BE / Link+HP+BE (the 8th is
    inferred) [V S14].
    - HP "cuts low frequencies in detector". The frequency is ~80 Hz [V S15] or 100 Hz [V~] **[C]**.
    - BE: "emphasized 6kHz band".
  - **Audio button, 6 states:** Norm / HP / Dist2 / Dist2+HP / Dist3 / Dist3+HP.
    - Audio HP: "3 dB down at 65Hz and −12 @ 30Hz. Its final slope is 18 dB per octave". The block diagram says
      "80 Hz High Pass".
  - EL8-X / Brit mod: **British Mode**, an emulation of 1176 all-buttons [V~].
- **Curves** [V S14]:
  - 1:1: no compression, just the "warming" circuits.
  - 2:1 and 3:1: "parabolic knees … won't typically go into hard limiting".
  - 4:1 and 6:1: "steeper knees … gradually move towards hard limiting".
  - 6:1 and 10:1: "shorter knee limiting".
  - Nuke: "brick wall … keeping any normal signal within 1 dB or so", with a **logarithmic release**.
  - 20:1: similar to Nuke with a different release slope.
- **Coloration.** Clean THD .025–.3 %. Dist 2/3 THD .1 %–20 %. Dist 2 emphasises the 2nd harmonic "especially while
  compressing". Dist 3 is dominated by the 3rd ("more similar to tape"). LEDs at 1 % and 3 % "Redline" [V S14]. SOS
  reports about 3 % 2nd harmonic in Dist 2 [V S15].

#### Shadow Hills Mastering Compressor
- **Topology.** **Optical stage (electroluminescent) → Discrete stage (VCA, feed-forward) → switchable output
  transformer** [V S18].
- **Optical stage** [V S18]:
  - Threshold: 24-position switch, 1 = minimum compression, 24 = maximum.
  - **Fixed 2:1**.
  - Gain: 24 positions, unity at about 7, finer near unity.
  - Two-stage release: "initial eighty percent … released very quickly … remaining twenty percent takes over a second".
- **Discrete stage** [V S18]:
  - Threshold: 24 positions.
  - **Ratio 1.2 → 6 in four steps + Flood = 20:1.** The list 1.2/2/4/6/Flood is inferred. Some sources show 3:1 [U].
  - **Attack: six settings, 0.1–30 ms.** Positions 2–5 are [U].
  - **Recover: six-step, 0.1–1.2 s, or Dual** (mimics the optical two-stage release). Intermediates [U].
  - Gain: 24 positions.
- **SC filter**: "no signal below 90Hz will affect" either stage [V S18].
- **Transformers** [V S18]:
  - **Nickel**: cleanest, a subtle ultra-HF lift.
  - **Iron**: an extra class-A stage, even harmonics, an "upper low frequency boost".
  - **Steel**: most distortion, "extremely tight boost in the low frequencies".
- Stereo / dual mono.

### 2.8 Digital styles (reference: FabFilter Pro-C 2/3)

**Pro-C 3 styles (14)** [V S30]:
- **Modern**: Clean ("allround, low distortion, feedforward, program dependent"), Versatile, Smooth, Punch, Upward
  (upward compression), TTM (multiband up+down).
- **Classic**: Op-El (opto-like tube), Vari-Mu ("feedback"), Classic ("vintage, feedback, very program dependent"),
  Opto ("slow, very soft knee, more linear").
- **Utility**: Vocal ("automatic knee and ratio"), Mastering ("as little harmonic distortion as possible"), Bus,
  Pumping ("deep and over-the-top … EDM").

**Pro-C 2 ranges** [V S30]:
- Threshold down to −60 dB.
- **Knee 0–72 dB.**
- **Attack 0.005–250 ms** (the Pro-C 3 page gives the same).
- **Lookahead up to 20 ms.**
- **Hold up to 500 ms.**
- **Mix 0–200 %.**
- Range (maximum GR).
- Up to 4× oversampling.
- **Stereo link 0–100 %, then Mid-only, Side-only, M>S, S>M.**
- Side-chain EQ (Bell, Low/High Shelf, Band Pass, Notch, Tilt).
- Auto release.
- Release range [U]: about 10 ms–2.5 s.

**Pro-C 3 additions** [V S30]: Character Off/Tube/Diode/Bright, with Drive and Pre/Post routing. Auto threshold.

**Look-ahead brickwall limiter** (generic):
- Ratio ∞.
- Attack is realised by the look-ahead window (a peak-hold/min filter plus smoothing) [U, standard practice].
- Release 1 ms–1 s with auto.
- True-peak detection needs about 4× oversampling [U].
- Ceiling in dBFS or dBTP.
- Latency equals the look-ahead, which B §7.4.5 says to fix across Modes.

**Pumping/EDM**: high ratio, long release (optionally tempo-synced), a hold of 0–500 ms, a strong SC HPF or an
external key, and often a feedback style [U].

---

## 3. Cross-cutting patterns (what the engine must support)

| Pattern | Units | Engine consequence |
|---|---|---|
| Input drives a fixed threshold | 1176, Distressor, TG1 Hold, Sta-Level, GML Hard | `Threshold` ghosted. `Input` is the compression amount and also drives coloration. |
| Side-chain gain as threshold | LA-2A/3A Peak Reduction, Fairchild AC Threshold | Threshold remapped to a 0–10 or 0–100 scale. The curve is fixed by the gain element. |
| Threshold ↔ ratio coupling | 1176 (ratio raises threshold), Altec 436C (2:1 @ 0 dBm → 4:1 @ +16 dBm) | The curve needs a Mode hook `ratioFromThreshold` / `thresholdOffsetFromRatio`. |
| Level-dependent ratio | Fairchild, Manley (Limit >12 dB), Sta-Level, 436, VSC-2 Soft, GML Soft, Distressor 2:1/3:1 | Gain computer type "progressive": slope s(x) rises with dB over threshold. |
| FB vs FF | FB: 1176, LA-2A, Fairchild, Manley, 525, Neve, alpha(default), 2500/527 "Old" | FB needs a one-sample-delayed loop or an iterative solve. The effective ratio is 1 + k. |
| Stepped time lists with Auto | SSL, VSC-2, 2254, 33609 a1/a2, Shadow Dual | `Release` list plus a `TimeMode`. The UI draws one knob with an extra "Auto" detent. |
| Coupled attack+release presets | Fairchild TC 1–6, TG1 Comp/Limit | `Release` shows a TC index. `Attack` is read-only and derived. |
| Program-dependent, no control | LA-2A, LA-3A, dbx 160, Level-Devil, 525 (frequency-dependent release) | Ballistics models with internal state (opto memory, rate limits). |
| Rate units | dbx 165A (dB/ms, dB/s), dbx 160 (≈125 dB/s) | The ParamSpec unit can be "dB/s". Convert internally. |
| Two stages | 33609, 2254 (shared element), Shadow Hills (serial), 165A PeakStop, alpha Soft Clip, Red 3 | A Stage-2 group. Topology per Mode: shared-element max-combine, serial pre, or post clip. |
| SC spectral shaping | Thrust (10 dB/decade; ±15 dB @ 20 Hz/20 kHz), R37 HF emphasis, 6 kHz BE, 525 De-Ess, 33609 Slow = 100 Hz HP | An `SCE` list per Mode, separate from the SC HPF. |
| Stereo link variety | CV-sum with both detecting (API, Manley, GML), SC-sum (Distressor Link), max (typical digital), % link (2500, Pro-C), Lat/Vert (Fairchild), M/S (Manley M/S, alpha) | Link %, stereo mode (LR/MS), and a per-Mode fixed link *law*. |
| Coloration selection | 1176 revisions, Distressor Audio, Shadow transformers, C2 Crush, alpha Warm, Manley tube type, Pro-C character | One `Color` stepped list per Mode. |
| Knob direction/scale | 1176 Attack/Release CW = faster. Knob-number scales: Distressor 0–10, 1176 0–48, Fairchild 0–10, Shadow 1–24 | ParamSpec `displayScale` and `hardwareDirection`. Offer a UiPreferences option "match hardware knob direction". |

---

## 4. (1) Universal parameter superset

B §7.4 decides that host parameters are continuous floats and each Mode quantises them. The IDs below are the slots.
The "host domain" is a suggestion for the physical range the slot must cover.

| ID | Name (UI) | Suggested host domain | Verdict | Why |
|---|---|---|---|---|
| `in` | Input / Drive | −24 … +36 dB (or a Mode knob scale) | **Keep** | It is the compression control for fixed-threshold units and the drive into coloration everywhere. It absorbs Pro-C "Drive", Distressor Input and 1176 Input. |
| `thr` | Threshold | −60 … +24 dBFS | **Keep** | Real thresholds, plus remaps (Peak Reduction, AC Threshold, 24-position Shadow switches). |
| `ratio` | Ratio | slope `s = 1−1/R` ∈ [0, 2] (1:1 … ∞ … −1:1) | **Keep** | Covers continuous, stepped, named (Compress/Limit), progressive and negative ratios. Display as "R:1", "∞:1", "−1:1" or the named step. |
| `knee` | Knee | 0 … 72 dB (Pro-C [V S30]) | **Keep** | API Hard/Med/Soft, 527 Hard/Soft, SSL-E OverEasy defeat, dbx 160 vs 160X, Fairchild DC threshold (remap), digital continuous. |
| `range` | Range (max GR) | 0 … 60 dB (Off = ∞) | **Keep** | mpressor GR limiter (0–21 dB [V S25]), alpha GR Limit, Pro-C Range. Cheap: a clamp on GR. |
| `atk` | Attack | 5 µs … 300 ms (log) | **Keep** | 0.005 ms (Pro-C) up to 300 ms (CL 1B). Also remap target for GML crest factor and CL 1B Fix/Man delay. |
| `rel` | Release | 1 ms … 30 s (log) | **Keep** | 20 s (Distressor Opto) and 25 s (Fairchild TC6). Also carries TC indices and position lists. |
| `tmode` | Time Mode | per-Mode stepped list | **Keep** | SSL/VSC Auto, Neve a1/a2, CL 1B Fixed/Manual/Fix-Man, 165A Auto/Manual, alpha/mpressor Auto-Fast & Anti-Log, E-series Log/Lin release, TG1 Comp/Limit, digital Manual/Auto/Sync. |
| `hold` | Hold | 0 … 500 ms | **Keep (digital only)** | Pro-C hold, Pump. Also GML Release Hysteresis (remap). Ghosted for hardware. |
| `look` | Lookahead | 0 … 20 ms | **Keep (digital only)** | Brickwall, Master Clear. Latency is fixed globally (B §7.4.5), so hardware Modes keep look = 0 behind a padded delay. |
| `det` | Detector | per-Mode list: Peak / RMS / Peak+RMS / Opto / Tube / Tri | **Keep, mostly locked** | Selectable on SSL 9000 (Peak/RMS) and digital Modes. Locked and displayed elsewhere, so the characteristics screen can label it. |
| `topo` | Topology | {FF, FB} | **Keep, mostly locked** | Switchable on API 2500/527 (New/Old), alpha (FF/FB) and the digital "Classic FB". |
| `schpf` | SC High-pass | Off, 20 … 500 Hz | **Keep** | Native on Manley (100 Hz), Shadow (90 Hz), C2 (150 Hz, 6 dB/oct), VSC-2 (60/90), Distressor (HP), Herchild (50/100/200/350). An *extension* elsewhere. |
| `sce` | SC Emphasis | per-Mode list or a continuous tilt | **Keep** | Thrust (Norm/Med/Loud; In/Out), R37/LA-3A HF emphasis, Distressor BE 6 kHz, 525 De-Ess, alpha LP↔HP morph. Digital: a tilt of ±6 dB/oct [U value]. |
| `link` | Stereo Link | 0 … 100 % | **Keep** | API 2500 IND/50–100; Pro-C 0–100; On/Off units snap to {0, 100}. |
| `lshape` | Link Shape | per-Mode list | **Keep (minor)** | API 2500+ Fast/Slow/Both; original 2500 Flat/HP/LP/BP. Ghosted elsewhere. |
| `stmode` | Stereo Mode | {Stereo, Mid/Side} (+ Mid-only, Side-only for digital) | **Keep** | Fairchild Lat/Vert, Manley M/S, alpha M/S, Pro-C mid/side. |
| `color` | Color / Voice | per-Mode stepped list | **Keep** | 1176 rev, Distressor Audio (6 states), Shadow transformers, C2 Crush, alpha Warm, Manley tube, dbx 160/160X, Pro-C character. |
| `mu` | Makeup / Output | −24 … +36 dB (or a Mode scale) | **Keep** | Every unit except where makeup is automatic (SSL channel). |
| `automu` | Auto Makeup | {Off, On} | **Keep** | API 2500 Auto/Manual, SSL channel (locked On), digital. |
| `mix` | Mix | 0 … 100 % (digital: … 200 %) | **Keep** | Native on 2500, alpha, CL 1B Mk II and Waves SSL. An extension elsewhere. Pro-C's 200 % [V S30] is allowed on digital Modes only. |
| `s2thr` | Stage 2 Threshold / Ceiling | −30 … +24 dBFS, plus Off | **Keep** | 33609/2254 limiter, 165A PeakStop, alpha Soft Clip, Red 3 limiter, Shadow optical stage. |
| `s2atk` | Stage 2 Attack | per-Mode list | **Keep** | 33609 limiter Fast/Slow. 2254 5 ms or fast. |
| `s2rel` | Stage 2 Release | per-Mode list | **Keep** | 33609 50–800 + a1/a2; 2254 100/200/800/Auto. |

**Global (not per Mode):** Mode, Output trim, Bypass, Oversampling, Key input (internal/external), Reference level
(dBu↔dBFS), SC Listen (monitor), and the delta/Ambience monitor.

**Merged:**
- Pro-C "Drive" goes into `in`.
- The API/525 "Ceiling" and the Manley "gang" become a UI gesture (drag `thr` and `mu` together), not a parameter.
- The GML Crest Factor Fast and Peak become one control remapped onto `atk`.
- Opto and discrete gains on the Shadow Hills are folded (§5.4 note M24).
- Distressor Detector HP/BE/Link become `schpf`/`sce`/`link`.
- The API Blend "parallel" law is dropped (see below).

**Dropped (v1):**
- Expander/gate (SSL channel, Level-Devil expansion). These are not compression. Revisit them as their own Modes.
- Niveau and tilt audio EQ (elysia). It is post-dynamics EQ.
- Waves Autofade.
- TMT channel variance, Mono Maker and Width.
- Tube balance and meter-zero trims.
- The API Blend "parallel (input + output)" law. Crossfade `mix` plus makeup covers it within a gain factor.
- Meter-source switches. These are UI state, not parameters.

---

## 5. (2) Mode × Parameter matrix

### 5.1 Cell notation

| Code | Meaning | UI treatment |
|---|---|---|
| `C a–b u (log/lin)` | continuous range | normal knob |
| `Cd a–b u /n` | continuous with n detents | knob with detent ticks |
| `S{…}` | stepped, exact list (display labels) | segmented cells (HR style, B §7.4) or a detented knob |
| `L x` | locked at x (shown, not editable) | dimmed value, visible |
| `P …` | locked, behaviour is program-dependent (described) | dimmed, with a live readout from telemetry |
| `—` | not applicable | ghosted |
| `R: …` | remapped: this slot drives a different physical quantity | normal control with a Mode-specific label |
| `+x` | extension, not on the original. Default in brackets | normal control with an "extra" tint |
| `·` | the **hardware default** for this column (below) | |

**Hardware defaults (`·`):**
- `in` = `+C −24…+24 dB [0]`
- `automu` = `—`
- `mix` = `+C 0–100 % [100]`
- `range` = `—`
- `hold` = `—`
- `look` = `L 0` (behind the global latency pad)
- `schpf` = `+C 20–500 Hz [Off]`
- `sce` = `—`
- `lshape` = `—`
- `stmode` = `+S{Stereo, M/S} [Stereo]`
- `s2*` = `—`

`[Cx]` / `[Ux]` inside a cell point to the conflict or unverified notes in §2.

Mode IDs (names are §6):
- M01 FET 76 (1176)
- M02 Opto 2A (LA-2A)
- M03 Opto 3A (LA-3A)
- M04 Opto Tube 1B (CL 1B)
- M05 Mu 67 (Fairchild 660/670)
- M06 Mu Mastering (Manley)
- M07 Mu Broadcast (Sta-Level)
- M08 Mu 36 (Altec 436C)
- M09 Bus G (SSL G bus)
- M10 Console E (SSL E/G channel)
- M11 Bus 25 (API 2500)
- M12 Module FB (API 525)
- M13 Module Tilt (API 527)
- M14 RMS 60 (dbx 160/160X)
- M15 RMS 65 (dbx 165A)
- M16 Diode 609 (33609)
- M17 Diode 54 (2254)
- M18 Octo (Distressor)
- M19 Zener Desk (TG1 / Zener Limiter)
- M20 Ruby 3 (Red 3)
- M21 Tri-Detector (GML 8900)
- M22 Bus Crush (Smart C2)
- M23 Bus Quad (VSC-2)
- M24 Twin Stage (Shadow Hills)
- M25 Class-A Master (elysia alpha)
- M26 Negative Ratio (mpressor)
- M27 Germanium 60 (Pye 4060)
- M28 Clean
- M29 Classic FB
- M30 Brickwall
- M31 Pump
- M32 Vocal Lift
- M33 Master Clear
- M34 Glue
- M35 Upward (future)

### 5.2 Table A: gain staging (`in`, `thr`, `mu`, `automu`, `mix`)

| Mode | `in` | `thr` | `mu` | `automu` | `mix` |
|---|---|---|---|---|---|
| M01 FET 76 | **R: compression amount.** C 0–48 knob scale (unity ≈24), gain into the fixed threshold | — (fixed internal threshold; offset rises with ratio 4→20) | C 0–48 knob scale (unity ≈24) | · | · |
| M02 Opto 2A | · | **R: Peak Reduction** C 0–100 (side-chain gain; 0 = no GR) | C Gain 0–100 [U: ≈40 dB] | · | · |
| M03 Opto 3A | + S{0, −20 dB} pad (reissue) + C [0] | **R: Peak Reduction** C 0–100 | C Gain 0–100 | · | · |
| M04 Opto Tube 1B | · | C +20 … −40 dB (+ Off) | C 0 … +30 dB | · | C 0–100 % (Mk II plugin) else · |
| M05 Mu 67 | S{−20 … 0 dB, 1 dB} (input attenuator) | **R: AC Threshold** C 0–10 (10 = none) | + C −12…+12 dB [0] (no output control on the original [U]) | · | · |
| M06 Mu Mastering | C input attenuator (Mastering version: stepped, count [U]) | C continuous (Mastering: S 21 steps; Limit ≈12 dB span, Compress ≈6 dB) | C output attenuator (Mastering: 0.5 dB steps, unity at −11.5) | · | · |
| M07 Mu Broadcast | **R: compression amount** C (into the fixed threshold) | — (fixed) | C Output | · | · |
| M08 Mu 36 | C Gain | C 0 … +16 dBm (output-referred). **Drives ratio (Table B)** | + C [0] | · | · |
| M09 Bus G | · | C −15 … +15 dB [C: ±20 on some] | C −5 … +15 dB | · | · (Waves: C 0–100 %, 0.1 % steps) |
| M10 Console E | · | C +10 … −20 dB | — (automatic) | L On (ratio- and threshold-derived) | · |
| M11 Bus 25 | · | C +20 … −20 dB (2500+; original +10 … −20 dBu) | C 0 … +24 dB (Manual) | S{Auto, Manual} | C 0–100 % (crossfade law; "parallel" law dropped) |
| M12 Module FB | · | C "Input (threshold)" [U range]; Ceiling = UI gang gesture | C Output [U range] | · | · |
| M13 Module Tilt | · | Cd +10 … −20 dB /31 | Cd −∞ … +10 dB /31 | · | · |
| M14 RMS 60 | · | C [U: −40 … +20 dB] | C [U: ±20 dB] | · | · |
| M15 RMS 65 | · | C −40 … +20 dBV | C [U] | · | · |
| M16 Diode 609 | · | S{−20 … +10 dBu, 2 dB} [U steps, from 2254] | S{0 … +20 dB} [U step 1 or 2 dB] | · | · |
| M17 Diode 54 | · | S{−20, −18 … +10 dBu} (2 dB) | S{0, 2 … 20 dB} (2 dB) | · | · |
| M18 Octo | **R: compression amount** C 0–10 (0 = off) | — (fixed per ratio curve) | C 0–10 (8 ≈ +4 dBu) | · | · |
| M19 Zener Desk | **R: Hold** (input+threshold) S21 positions (Zener: + High/Low 12 dB) | — | S{−10 … +10 dB, 1 dB} (21 positions) | · | · |
| M20 Ruby 3 | · | C [C/U: range] | C [U] | · | · |
| M21 Tri-Detector | · | C. **In Hard-knee `ratio` positions it acts as input gain into a fixed threshold** (S23) | C Output [U range] | · | · |
| M22 Bus Crush | · | C −20 … +20 dB | C 0 … +20 dB | · | · |
| M23 Bus Quad | · | C −22 … +22 dBu [V~] | C 0 … +22 dB [V~] | · | · |
| M24 Twin Stage | · | S{1 … 24} (discrete stage; 24 = max) | S{1 … 24} discrete gain (unity ≈7; finer near unity) | · | · (PA plugin: C) |
| M25 Class-A Master | · | Cd /21 [U range] | Cd Gain /21 [U] | · | C 0–100 % (Direct/Compressed/Mix) |
| M26 Negative Ratio | · | C [U] | C Gain [U] | · | · |
| M27 Germanium 60 | · | C −20 … +16 dB (stepped? [U]) | C [U] | · | · |
| M28 Clean | C −24 … +24 dB | C −60 … 0 dBFS | C −24 … +24 dB | S{Off, On} | C 0–200 % |
| M29 Classic FB | C −24 … +24 dB | C −60 … 0 dBFS | C −24 … +24 dB | S{Off, On} | C 0–200 % |
| M30 Brickwall | C 0 … +24 dB (drive) | C −30 … 0 dBFS (limit threshold) | **R: Ceiling** C −12 … 0 dBFS (dBTP when true-peak is on) | L On | L 100 % (+) |
| M31 Pump | C −24 … +24 dB | C −60 … 0 dBFS | C −24 … +24 dB | S{Off, On} | C 0–100 % |
| M32 Vocal Lift | C −24 … +24 dB | C −60 … 0 dBFS (the only curve control) | C | L On | C 0–100 % |
| M33 Master Clear | C −12 … +12 dB | C −40 … 0 dBFS | C −12 … +12 dB | S{Off, On} | C 0–100 % |
| M34 Glue | C −24 … +24 dB | C −40 … 0 dBFS | C −12 … +24 dB | S{Off, On} | C 0–100 % |
| M35 Upward | C | C (upward threshold) | C | L On | C |

### 5.3 Table B: static curve (`ratio`, `knee`, `range`)

| Mode | `ratio` | `knee` | `range` |
|---|---|---|---|
| M01 FET 76 | S{4, 8, 12, 20, **All** (≈12–20 program-dependent, bias shift, attack lag)} | P (FB-derived; 4:1 softer, 20:1 near-hard [U]) | · |
| M02 Opto 2A | S{**Compress** (≈3:1 program) , **Limit** (≈10:1+ program)} [C: 4:1] | P very soft (T4 program- and frequency-dependent) | · |
| M03 Opto 3A | S{Compress, Limit} (near-identical except heavy GR) | P soft | · |
| M04 Opto Tube 1B | C 2 … 10 :1 (lin) | P soft opto [U] | · |
| M05 Mu 67 | P progressive (≈1–2:1 at onset → ≈20:1) [C: 30:1] | **R: DC Threshold** C 0–10 (CCW = hard peak-limit … CW = gentle) | · |
| M06 Mu Mastering | S{**Compress** 1.5:1 soft, **Limit** 4:1 → 20:1 beyond ≈12 dB GR} | P (softens as GR increases) | · |
| M07 Mu Broadcast | P ≈3:1 progressive | P soft | L 40 dB (max GR) |
| M08 Mu 36 | **R: derived from `thr`**, 2:1 @ 0 dBm → 4:1 @ +16 dBm (interpolate [U: law]) | P soft vari-mu | L 30 dB |
| M09 Bus G | S{2, 4, 10} | L fixed [U: shape] | · |
| M10 Console E | C 1 … ∞ (∞ = limiter) | S{OverEasy, Hard} (E module defeat switch) | · |
| M11 Bus 25 | S{1.5, 2, 3, 4, 6, 10, ∞} | S{Hard, Med, Soft} | · |
| M12 Module FB | S{Off (GR off), 2 (Comp), 20 (Limit)} | P (FB, peak) | L 25 dB |
| M13 Module Tilt | Cd 1 … ∞ /31 | S{Hard, Soft} | · |
| M14 RMS 60 | C 1 … ∞ … −1 (s ∈ [0, 2]; negative part only with the 160X voice) | S{Hard (160), OverEasy (160X)} | L >60 dB |
| M15 RMS 65 | C 1 … ∞ | L OverEasy | · |
| M16 Diode 609 | S{1.5, 2, 3, 4, 6} (true ratio reached >5 dB over threshold) | P progressive ≈5 dB | · |
| M17 Diode 54 | S{1.5, 2, 3, 4, 6} | P ≈10 dB transition | · |
| M18 Octo | S{1, 2, 3, 4, 6, 10 (Opto), 20, Nuke} (+ S{Brit} with the EL8-X voice) | P per ratio: 2, 3 parabolic; 4, 6 steeper; 6, 10 short; 20/Nuke hard | · |
| M19 Zener Desk | L 2:1 (TG1, both modes) / Zener: follows `tmode` {Comp 1 2:1, Comp 2, Limit} [U: ratios for Comp 2 and Limit] | P | · |
| M20 Ruby 3 | C 1.5 … ∞ | P [U] | · |
| M21 Tri-Detector | S{**Soft** (auto, rising)} ∪ C 1.5 … ∞ (hard-knee region) | R: ← `ratio` (Soft position = soft knee, else hard) | · |
| M22 Bus Crush | S{1.5, 2, 3, 4, 10, Limit} | L [U] | · |
| M23 Bus Quad | S{Soft (1→8 rising), 2, 4, 8, 10, Brick (40)} | R: ← `ratio` (Soft) | · |
| M24 Twin Stage | S{1.2, 2, 4, 6, Flood (20)} [U: whether 3:1 exists] | L [U] | · |
| M25 Class-A Master | Cd /21. FB: up to ≈2.5:1. FF: printed ×2, reaching limiting and negative [V~] | P | + C 0 … 30 dB [Off] (plugin GR Limit) |
| M26 Negative Ratio | C 1 … ∞ … negative (s up to 2 [U: true limit]) | P [U] | C 0 … 21 dB (GR limiter) |
| M27 Germanium 60 | S{1, 2, 3, 5, Limit} | P [U] | · |
| M28 Clean | C 1 … ∞ (s ∈ [0, 1]) | C 0 … 72 dB | C 0 … 60 dB [Off] |
| M29 Classic FB | C 1 … ∞ | C 0 … 72 dB | C 0 … 60 dB |
| M30 Brickwall | L ∞ | C 0 … 6 dB [U range] | — |
| M31 Pump | C 2 … ∞ | C 0 … 24 dB | C 0 … 60 dB |
| M32 Vocal Lift | P auto (derived from `thr`, Pro-C Vocal behaviour) | P auto | C 0 … 30 dB |
| M33 Master Clear | C 1 … 4 :1 [design choice] | C 0 … 36 dB | C 0 … 12 dB |
| M34 Glue | S{1.5, 2, 4, 10} (+C) [design choice] | C 0 … 24 dB | C |
| M35 Upward | C 1 … ∞ (applied *below* threshold) | C | C (max upward gain) |

### 5.4 Table C: time (`atk`, `rel`, `tmode`, `hold`, `look`)

| Mode | `atk` | `rel` | `tmode` | `hold` / `look` |
|---|---|---|---|---|
| M01 FET 76 | C 20 → 800 µs (log) **+ "Off" end-stop** (GR disabled, transformers on). Hardware CW = faster. The stereo link raises the minimum to 40 µs | C 50 → 1100 ms (log). Hardware CW = faster | — | · |
| M02 Opto 2A | P ≈10 ms average | P two-stage: ≈60 ms to 50 %, then 1–15 s depending on history [C: ≤2 s] | — | · |
| M03 Opto 3A | P ≤1.5 ms | P ≈60 ms to 50 %, then program (slower when driven −7…−10 dB) | — | · |
| M04 Opto Tube 1B | C 0.5 … 300 ms (log). **In Fix/Man: R = delay before manual release** | C 0.05 … 10 s (log) | S{**Fixed** (1 ms / 50 ms), **Manual**, **Fix/Man**} | · |
| M05 Mu 67 | R: read-only, derived from the TC: {0.2, 0.2, 0.4, 0.8 [C: 0.4], 0.4, 0.2 ms} | **R: TC selector S{1: 0.3 s, 2: 0.8 s, 3: 2 s, 4: 5 s, 5: P 2 s/10 s, 6: P 0.3 s/10 s/25 s}** | — (TC5/6 imply program release) | · |
| M06 Mu Mastering | C 25 … 70 ms (Mastering: S 11 positions, range slightly extended [U values]) | S{0.2, 0.4, 0.6, 4, 8 s} | — | · |
| M07 Mu Broadcast | P fast (unquantified [U]) | P (faster at higher GR) | S{Single, Double} (+ Retro: Triple; 6-position release [U values]) | · |
| M08 Mu 36 | L 50 ms | C 0.3 … 1.3 s (63 % recovery) | — | · |
| M09 Bus G | S{0.1, 0.3, 1, 3, 10, 30 ms} | S{0.1, 0.3, 0.6, 1.2 s} + the Auto position lives in `tmode` | S{Manual, **Auto**} (UI: 5th release detent) | · |
| M10 Console E | S{**Auto** (P 3–30 ms), **Fast** 1 ms} | C 0.1 … 4 s (log) | S{Log release, Linear release} (E-module switch) | · |
| M11 Bus 25 | S{0.03, 0.1, 0.3, 1, 3, 10, 30 ms} | S{0.05, 0.1, 0.2, 0.5, 1, 2 s} **∪ Var C 0.05 … 3 s** | S{Fixed, Variable} (UI: 7th switch position = variable pot) | · |
| M12 Module FB | L 15 µs | S{0.1, 0.5, 2.0, 2.5 s} [C: 0.1/0.5/1.5/2.0]. P frequency-dependent (HF releases faster) | — | · |
| M13 Module Tilt | Cd 1 … 25 ms /31 | Cd 0.3 … 3 s /31 | — | · |
| M14 RMS 60 | P 15 ms @10 dB, 5 ms @20 dB, 3 ms @30 dB over threshold | P ≈120–125 dB/s (8 ms/1 dB, 80 ms/10 dB, 400 ms/50 dB) | — | · |
| M15 RMS 65 | C rate 400 → 1 dB/ms (display as rate) | C rate 10 → 4000 dB/s | S{Auto (160-style P), Manual} | · |
| M16 Diode 609 | S{Fast ≈3 ms, Slow ≈6 ms} (Slow also switches `schpf`: see Table D) | S{100, 400, 800, 1500 ms} | S{Manual, **a1** (100 ms/2 s), **a2** (50 ms/5 s)} | · |
| M17 Diode 54 | L 5 ms (+ S{5 ms, Fast C 0.1–2 ms} on /R) | S{100, 200, 800 ms} [C: 400/800/1500] | S{Manual, Auto (attack auto too)} | · |
| M18 Octo | C 0–10 knob = 50 µs … 30 ms (0 = fastest) [C: 50 ms max] | C 0–10 knob = 0.05 … 3.5 s. With `ratio`=10 (Opto) the maximum becomes 20 s. P log release on Nuke | — | · |
| M19 Zener Desk | TG1: L per `tmode` (Comp 47 ms, Limit 8 ms). Zener voice: S11 positions (pos 2 original; pos 1 = 28 ms Comp1 / 5 ms Limit) [other values U] | **Conditional S6:** Limit {0.05, 0.1, 0.25, 0.5, 1, 2 s}; Comp {0.25, 0.5, 1.2, 2.5, 5, 10 s}. Zener voice: 21 positions [U values] | S{Comp, Limit} (Zener voice: {Comp 1, Comp 2, Limit}) | · |
| M20 Ruby 3 | C [U] | C 0.1 … 4 s | S{Manual, Auto} | · |
| M21 Tri-Detector | **R: Crest factor** (Fast + Peak combined; higher = more transient) | **R: Timing** (RMS attack/release macro) | — | **R: `hold` = Release Hysteresis** / look · |
| M22 Bus Crush | S{≈0.3 ("0"), 0.1, 0.3, 1, 3, 10, 30 ms} [C: first step] | S{0.1, 0.3, 0.6, 1.2, 2.4 s} [C: 6th step?] | — | · |
| M23 Bus Quad | S{0.1, 0.3, 1, 3, 10, 30 ms} [U: intermediates] | S{0.1, 0.3, 0.6, 1.2 s, (6th [U])} | S{Manual, Auto} | · |
| M24 Twin Stage | S6 {0.1 … 30 ms} [U: positions 2–5] | S5 {0.1 … 1.2 s} [U: intermediates] | S{Manual, **Dual** (opto-like two-stage)} | · |
| M25 Class-A Master | Cd /21 [U range] | Cd /21 (time to recover from 6 dB GR) [U range] | S{Off, AF-Atk, AF-Rel, AF-Both} (Auto Fast) | · |
| M26 Negative Ratio | C [U] | C [U] | S{Normal, Anti-Log, Auto-Fast, Anti-Log+AF} | · |
| M27 Germanium 60 | L (fixed; value [U]) | S{100, 200, 400, 800, 1600 ms} | — | · |
| M28 Clean | C 0.005 … 250 ms (log) | C 5 ms … 5 s (log) | S{Manual, Auto} | C 0–500 ms / C 0–20 ms |
| M29 Classic FB | C 0.01 … 250 ms | C 5 ms … 5 s | S{Manual, Auto} | C 0–500 / C 0–20 ms |
| M30 Brickwall | R: ← `look` (attack realised inside the window) | C 1 ms … 1 s | S{Manual, Auto} | C 0–50 ms / **C 0.5–20 ms** |
| M31 Pump | C 0.1 … 100 ms | C 50 ms … 2 s, or **note values when `tmode` = Sync** {1/32 … 1 bar} | S{Manual, Sync} | C 0–500 ms / + |
| M32 Vocal Lift | C 0.1 … 50 ms | C 20 ms … 1 s | S{Manual, Auto} | — / C 0–10 ms |
| M33 Master Clear | C 0.1 … 100 ms | C 20 ms … 3 s | S{Manual, Auto} | C / C 0–20 ms |
| M34 Glue | S{0.1, 0.3, 1, 3, 10, 30 ms} (+C) | S{0.1 … 1.2 s} (+C) | S{Manual, Auto} | — / — |
| M35 Upward | C | C | S{Manual, Auto} | C / C |

M24 note: the Shadow Hills *optical* stage timing lives in Table E (`s2atk` L slow, `s2rel` P two-stage). Optical
Gain is folded into `s2thr`. Raising the optical gain raises the discrete stage's detector input, which is equivalent
to lowering the discrete threshold, but only as an approximation, because the opto output level also changes the
output. If exactness matters later, add `s2mu`.

### 5.5 Table D: detection and side chain (`det`, `topo`, `schpf`, `sce`, `link`/`lshape`, `stmode`)

| Mode | `det` | `topo` | `schpf` | `sce` | `link` / `lshape` | `stmode` |
|---|---|---|---|---|---|---|
| M01 FET 76 | L Peak | L FB | · | — | S{0, 100} (1176SA; see atk note) / — | · |
| M02 Opto 2A | L Opto (T4B, memory) | L FB [U] | · | **C R37 HF emphasis** 0 … max (LF desensitised up to 10 dB; >1 kHz emphasis) | S{0, 100} / — | · |
| M03 Opto 3A | L Opto (T4) | L FB [U] | · | C HF sensitivity (rear pot) | S{0, 100} / — | · |
| M04 Opto Tube 1B | L Opto | L [U] | · (Mk II: C SC low cut, freq [U]) | — | S{0, 100} (SC bus) / — | · |
| M05 Mu 67 | L Tube side-chain rectifier [U: type] | L FB | + S{Off, 50, 100, 200, 350 Hz} (Herchild precedent) | — | S{Ind, Link} / — | **S{L/R, Lat/Vert (M/S)}** (AGC) |
| M06 Mu Mastering | L Tube (remote cut-off) | L FB | **S{Off, 100 Hz (−3 dB)}** | — | S{Sep, Link (DC CV combine)} / — | S{Stereo, M/S} (M/S version) |
| M07 Mu Broadcast | L Tube (6AL5 rectifier) | L FB | · | — | S{0, 100} / — | · |
| M08 Mu 36 | L Tube (6AL5) | L [U] | · | — | + / — | · |
| M09 Bus G | L [U: peak-ish] | L FF | · (not on the original G [U]) | — | L 100 (single SC [U]) / — | · |
| M10 Console E | S{RMS, Peak} (9000 J/K switch) [C: E = RMS vs peak] | L FF | **R: channel HPF into SC** C [U range], 18 dB/oct | + S{Off, SC EQ} (E: whole EQ, G: filters) — v1: — | S{0, 100} (link to adjacent) / — | · |
| M11 Bus 25 | L RMS (THAT 2252) | **S{New (FF), Old (FB)}** | · | **S{Norm, Med, Loud}** (Thrust; Loud = 10 dB/decade) [C: SOS 2/4 dB/oct] | **S{Ind, 50, 60, 70, 80, 90, 100 %}** / **S{Fast (peak), Slow (RMS), Both}** (orig: {Flat, HP, LP, BP}) | · |
| M12 Module FB | L Peak | L FB | · | **S{Off, De-Ess}** (inverse voice-energy curve) | S{0, 100} / — | · |
| M13 Module Tilt | L RMS | **S{New (FF), Old (FB)}** | · | **S{Out, In}** (Thrust: −15 dB @ 20 Hz … +15 dB @ 20 kHz) | S{0, 100} (DC sum bus) / — | · |
| M14 RMS 60 | L True RMS | L FF | · | — | S{0, 100} / — | · |
| M15 RMS 65 | L RMS | L FF | · | + (detector input for pre-emphasis) | S{0, 100} (strap) / — | · |
| M16 Diode 609 | L [U: peak-ish] | L FB | **R: ← `atk`=Slow** (100 Hz 1st order); otherwise · | — | S{Dual mono, Stereo (SC linked)} → {0, 100} / — | · |
| M17 Diode 54 | L [U] | L FB | · | — | S{0, 100} (SC outputs combined) / — | · |
| M18 Octo | L VCA detector (special for 2/10/Nuke) | L [U] | **S{Off, HP}** (≈80–100 Hz [C]) | **S{Off, BE 6 kHz}** | S{0, 100} (Link sums both inputs) / — | · |
| M19 Zener Desk | L | L [U] | + (Zener plugin: C 30–300 Hz) | — | S{0, 100} / — | · |
| M20 Ruby 3 | L [U] | L [U] | · | — | S{0, 100} / — | · |
| M21 Tri-Detector | L Tri (SlowRMS + FastRMS + Peak) | L FF | · | — | S{0, 100} (Stereo Couple = CV cross-combine) / — | · |
| M22 Bus Crush | L [U] | L FF | **S{Off, 150 Hz 6 dB/oct}** | — | S{0, 100} / — | · |
| M23 Bus Quad | L [U] | L FF | **S{Off, 60, 90, …}** [U: full list] | — | S{Dual, Stereo} → {0, 100} / — | · |
| M24 Twin Stage | L Opto + VCA | L FF (discrete) | **S{Off, 90 Hz}** (feeds both stages) | — | S{Dual, Stereo} / — | · |
| M25 Class-A Master | L [U] | **S{FB, FF}** (default FB) | R: SC filter morph C LP ↔ HP, 30–3300 Hz (single knob + freq; `sce` holds the LP↔HP amount) | R: SC Gain (LP … shelves … HP) | S{0, 100} (plugin: C 0–100 %) / — | **S{Stereo, M/S}** |
| M26 Negative Ratio | L [U] | L FF | · | — | S{0, 100} / — | · |
| M27 Germanium 60 | L [U] | L [U] | · | — | · / — | · |
| M28 Clean | **S{Peak, RMS, Peak+RMS}** | L FF | C 20–500 Hz [Off] | C tilt ±6 dB/oct [design] | C 0–100 % / — | **S{Stereo, Mid, Side, M>S, S>M}** |
| M29 Classic FB | S{Peak, RMS} | L FB | C [Off] | C tilt | C 0–100 % / — | S{Stereo, Mid, Side} |
| M30 Brickwall | S{Sample peak, True peak (≥4× OS)} | L FF | — (+) | — | C 0–100 % [100] / — | S{Stereo, Mid, Side} |
| M31 Pump | S{Peak, RMS} | S{FF, FB} | C 20–500 Hz [100 Hz] | — | C 0–100 % [100] / — | · |
| M32 Vocal Lift | L RMS | L FF | C [80 Hz] | C (de-ess-like emphasis) [design] | C / — | · |
| M33 Master Clear | S{RMS, Peak+RMS} | L FF | C [Off] | C tilt | C [100] / — | S{Stereo, Mid, Side, M>S, S>M} |
| M34 Glue | L RMS | L FF | C [Off] | — | L 100 / — | · |
| M35 Upward | S{Peak, RMS} | L FF | C | C | C | · |

### 5.6 Table E: coloration and stage 2 (`color`, `s2thr`, `s2atk`, `s2rel`)

| Mode | `color` | `s2thr` | `s2atk` | `s2rel` |
|---|---|---|---|---|
| M01 FET 76 | **S{Rev A "blue" (class-A output, no LN), Rev D/E LN (default), Rev F/H (push-pull output)}** | — | — | — |
| M02 Opto 2A | L tube + UTC transformers | — | — | — |
| M03 Opto 3A | L class-A solid state + transformers | — | — | — |
| M04 Opto Tube 1B | L tube | — | — | — |
| M05 Mu 67 | L (660 = mono voice of the same channel) | — | — | — |
| M06 Mu Mastering | **S{5670, 6BA6 "T-Bar" (6386-like)}** | — | — | — |
| M07 Mu Broadcast | L (Level-Devil expander not supported) | — | — | — |
| M08 Mu 36 | L | — | — | — |
| M09 Bus G | L (+ S{Analog, Clean} extension, Waves precedent) | — | — | — |
| M10 Console E | S{E, G} [U: audible difference in dynamics; SSL says the E→G change was a "tighter response"] | — | — | — |
| M11 Bus 25 | L (2510/2520 + output transformer) | — | — | — |
| M12 Module FB | L | — | — | — |
| M13 Module Tilt | L | — | — | — |
| M14 RMS 60 | **S{160 (hard knee), 160X (OverEasy, ∞+)}** (drives `knee` and negative `ratio` availability) | — | — | — |
| M15 RMS 65 | L | **C −2 … +24 dBm PeakStop** (Off at top) | L instant [U] | L [U] |
| M16 Diode 609 | L | **S{+4 … +15 dBu}** [U: 1 dB steps] + Off (limit in/out) | **S{Fast ≈2 ms, Slow ≈4 ms}** | **S{50, 100, 200, 800 ms, a1, a2}** [U: list inside 50–800] |
| M17 Diode 54 | L | **S{+4 … +20 dBu, 2 dB}** + Off | L 5 ms (+ fast C 0.1–2 ms) | **S{100, 200, 800 ms, Auto}** |
| M18 Octo | **S{Clean, HP, Dist2, Dist2+HP, Dist3, Dist3+HP}** (audio HP −3 dB @ 65 Hz, 18 dB/oct) (+ Brit voice flag) | — | — | — |
| M19 Zener Desk | S{TG1, Zener} (voice: selects the time lists) (+ THD toggle) | — | — | — |
| M20 Ruby 3 | L | C limiter threshold [U range] + Off | L [U] | L [U] |
| M21 Tri-Detector | L | — | — | — |
| M22 Bus Crush | **S{Normal, Crush (FET stage + fixed bright EQ)}** | — | — | — |
| M23 Bus Quad | L | — | — | — |
| M24 Twin Stage | **S{Nickel, Iron, Steel}** | **R: Optical threshold S{Off, 1 … 24}** (serial, pre-VCA, fixed 2:1) | L slow opto [U value] | P two-stage (80 % fast, 20 % >1 s) |
| M25 Class-A Master | **S{Off, Warm (transformer)}** | **C Soft Clip threshold** (+Off) [U range; +21 dBu used in test] | L (clipper) | L |
| M26 Negative Ratio | L | — (GR limiter lives in `range`) | — | — |
| M27 Germanium 60 | L | — | — | — |
| M28 Clean | **S{Off, Tube, Diode, Bright}** (Pro-C 3 precedent; drive = `in`) | + C safety clip [Off] | — | — |
| M29 Classic FB | S{Off, Tube, Diode, Bright} | + | — | — |
| M30 Brickwall | S{Transparent, Loud (soft-clip pre-stage)} [design] | — | — | — |
| M31 Pump | S{Off, Tube, Diode, Bright} | + | — | — |
| M32 Vocal Lift | S{Off, Tube, Bright} | — | — | — |
| M33 Master Clear | L Off | + C output ceiling (true-peak) [Off] | L | C |
| M34 Glue | S{Off, Console} | — | — | — |
| M35 Upward | S{Off, …} | — | — | — |

### 5.7 How the matrix maps onto a descriptor

These are delta proposals to B §7.4's `ModeDescriptor` (`Kind{continuous, stepped, fixed, unused}`):

```cpp
enum class Kind : uint8_t { Continuous, Stepped, Fixed, Program, Unused, Remapped };

struct Step { float plain; const char* label; uint16_t tag; };  // tag: e.g. AUTO, TC5, OFF, VAR

struct ParamSpec {
    Kind kind;
    float lo, hi, skew;                  // Continuous (also a Stepped "∪ Var" sub-range)
    std::span<const Step> steps;         // Stepped
    int16_t detents = 0;                 // Continuous with detents (API 527 = 31, elysia = 21)
    float fixed;                         // Fixed
    bool extension = false;              // "+" cells: available, tinted, default neutral
    bool hardwareReversed = false;       // 1176 attack/release CW = faster
    const char* displayScale = nullptr;  // "0–48", "0–10", "1–24": knob numbers instead of physical units
    const char* remapLabel = nullptr;    // "Peak Reduction", "AC Threshold", "DC Threshold", "Hold", "Timing"
    uint8_t dependsOn = 0xFF;            // param whose value selects an alternate step list (TG1 rel ← tmode)
    std::span<const std::span<const Step>> altSteps;
};
```

- **Conditional lists.** M19 `rel` depends on `tmode`. M18 `rel` hi depends on `ratio` (Opto 20 s). M14 negative
  `ratio` depends on `color`. M16 `schpf` depends on `atk`. M21 `thr` meaning depends on `ratio`. That is five
  dependencies, so this has to be general, not a special case.
- **Composite UI knobs.** The M09 release knob draws `rel` steps plus `tmode` = Auto as a 5th detent. The M05 TC knob
  writes `rel` (index) and shows the derived `atk`. The M11 release draws 6 steps plus a Var position that exposes the
  continuous sub-range. The UI writes the underlying params inside one gesture (B §7.4.2).
- **Ratio.** Store `s = 1 − 1/R`. The FF gain computer uses `GR = s·over` (GlueCompressor `curveGrDb`, B §6.1). The FB
  engine uses `k = s/(1−s)`, so that the steady-state ratio matches the label. This matters for M11/M13/M25 when
  `topo` switches, if the Mode wants "same label, same static ratio". API instead describes Old as "smoother, softer",
  which suggests a *different* static curve [V S6]. Decision per Mode: M11/M13 keep the label and let FB soften the
  knee. M25 follows elysia's "×2 in FF" [V S24].

---

## 6. (3) Mode names (trademark-avoiding) and implementation order

### 6.1 Names

Guidance, not legal advice:
- Avoid manufacturer names, product names and logos: Fairchild, Teletronix, UREI, UA, SSL, API, Neve, Manley,
  Distressor, Empirical, Tube-Tech, Shadow Hills, elysia, alpha, mpressor, GML, Focusrite/Red, Vertigo, Smart, Pye,
  Gates, Altec, EMI, TG, Abbey Road, "Lunchbox" (API), "Thrust" (an API patent and name).
- Bare model numbers are used descriptively by several plug-in vendors, but they carry some risk [U]. The names below use
  at most a two-digit fragment or a descriptive word. Keep the "inspired by" text out of the product UI and presets.

| ID | Proposed name | Alternative | Family | Inspired by |
|---|---|---|---|---|
| M01 | **FET 76** | FET Peak | FET | 1176 (A…H via Color) |
| M02 | **Opto 2A** | Opto Leveler | Opto | LA-2A |
| M03 | **Opto 3A** | Opto Solid | Opto | LA-3A |
| M04 | **Opto Tube 1B** | Opto Studio | Opto | CL 1B |
| M05 | **Mu 67** | Vari-Mu Six | Vari-mu | Fairchild 660/670 |
| M06 | **Mu Mastering** | Mu M | Vari-mu | Manley Variable Mu |
| M07 | **Mu Broadcast** | Mu Leveler | Vari-mu | Gates Sta-Level |
| M08 | **Mu 36** | Tube Leveler | Vari-mu | Altec 436C |
| M09 | **Bus G** | Console Bus | VCA bus | SSL G bus |
| M10 | **Console E** | Channel E/G | VCA channel | SSL E/G channel |
| M11 | **Bus 25** | Punch Bus | VCA bus | API 2500 |
| M12 | **Module FB** | Feedback 5 | VCA/FB | API 525 |
| M13 | **Module Tilt** | Module 27 | VCA | API 527 ("Thrust" avoided) |
| M14 | **RMS 60** | Over 60 | VCA | dbx 160/160X |
| M15 | **RMS 65** | Over 65 | VCA | dbx 165A |
| M16 | **Diode 609** | Diode Twin | Diode | Neve 33609 |
| M17 | **Diode 54** | Diode Module | Diode | Neve 2254 |
| M18 | **Octo** | Multi-Curve 8 | Hybrid VCA | Distressor |
| M19 | **Zener Desk** | Zener 13 | Zener | EMI TG12413 / Chandler |
| M20 | **Ruby 3** | Class-A VCA | VCA | Focusrite Red 3 |
| M21 | **Tri-Detector** | Crest 89 | VCA | GML 8900 |
| M22 | **Bus Crush** | Bus C | VCA bus | Smart C2 |
| M23 | **Bus Quad** | Quad Discrete | VCA bus | Vertigo VSC-2 |
| M24 | **Twin Stage** | Opto+VCA Master | Hybrid | Shadow Hills MC |
| M25 | **Class-A Master** | Discrete FF/FB | Discrete | elysia alpha |
| M26 | **Negative Ratio** | Anti-Log | Discrete | elysia mpressor |
| M27 | **Germanium 60** | Olympic-era Class A | Transistor | Pye 4060 |
| M28 | **Clean** | — | Digital | Pro-C Clean-like |
| M29 | **Classic FB** | — | Digital | Pro-C Classic-like |
| M30 | **Brickwall** | — | Digital | look-ahead limiter |
| M31 | **Pump** | — | Digital | Pro-C Pumping / EDM |
| M32 | **Vocal Lift** | — | Digital | Pro-C Vocal |
| M33 | **Master Clear** | — | Digital | Pro-C Mastering |
| M34 | **Glue** | — | Digital | Pro-C Bus |
| M35 | **Upward** | — | Digital (future) | Pro-C 3 Upward/TTM |


### 6.2 Engine surface (features forced by Modes)

| F# | Engine feature | First forced by | Also used by |
|---|---|---|---|
| F1 | Log-domain FF gain computer, quadratic knee, slope ratio, range | M28 Clean | all FF Modes |
| F2 | ModeDescriptor: stepped, fixed, extension, ghosting, displayScale, reversed knobs | M09 Bus G | all |
| F3 | Remaps (Input→compression, side-chain-gain thresholds, TC selector, DC-threshold→knee) | M01 FET 76, M05 Mu 67 | M02/03/07/18/19/21 |
| F4 | Feedback topology (closed loop, `k = s/(1−s)`), and the FB static-curve solver for the display | M01 FET 76 | M02 M05–M08 M11–M13 M16 M17 M25 M29 |
| F5 | Program-dependent release: dual TC (SSL Auto), a1/a2, TC5/6 multi-stage, Dual | M09 Bus G | M05 M16 M17 M23 M24 |
| F6 | Opto cell model (two-stage release with memory, level-dependent speed) | M02 Opto 2A | M03 M04 M18(10:1) M24(stage 2) |
| F7 | Progressive / level-dependent ratio; threshold↔ratio coupling | M05 Mu 67 | M06–M08 M16 M17 M18(2,3) M21 M23 |
| F8 | RMS detector; rate-based ballistics (dB/s); crest multi-detector | M11 Bus 25 | M13–M15 M21 M28 |
| F9 | SC filters: HPF, pink-tilt, HF shelf emphasis, 6 kHz bell, de-ess curve | M11 (tilt), M02 (R37) | M06 M12 M13 M16 M18 M22–M25 |
| F10 | Link variants (% link, CV-sum-with-own-detectors, max, SC-sum), Lat/Vert / M/S | M11 (%), M05 (Lat/Vert) | all stereo |
| F11 | Stage 2: shared-element max-combine, serial pre-stage, post clipper | M16 Diode 609 | M15 M17 M20 M24 M25 |
| F12 | Coloration: FET nonlinearity, tube push-pull, transformer, diode bridge, harmonic generator, clipper | M01, M02, M05, M16 | M18 (Dist2/3), M24 (3 transformers), M22 (Crush) |
| F13 | Look-ahead, fixed global latency, true-peak (4× OS) | M30 Brickwall | M28 M33 |
| F14 | Oversampling for nonlinear Modes (global) | M01 | M18 M24 M30 |
| F15 | Tempo-synced release (host BPM) | M31 Pump | — |
| F16 | Negative ratio (s > 1), GR clamp | M26, M14 (160X) | M25 FF |
| F17 | Upward compression (gain *below* threshold) | M35 | — |

### 6.3 Recommended order

**Sprint-1 set (first 8).** Ranked by value and by how much engine they force early:

| # | Mode | Why now | Forces |
|---|---|---|---|
| 1 | **M28 Clean** | Reference Mode and golden-file baseline. Validates the gain computer, the transfer-curve screen, link, SC HPF, mix and range with all-continuous params. | F1, F13 (look), F9 (HPF), F10 (%) |
| 2 | **M09 Bus G** | Most-used bus compressor. First stepped Mode (lists plus an Auto detent). GlueCompressor seed exists (B §6.1). | F2, F5 |
| 3 | **M01 FET 76** | Most-used tracking compressor. Forces input→compression remap, reversed knobs, the All-buttons step, the Off end-stop, the FB loop and FET color. | F3, F4, F12, F14 |
| 4 | **M02 Opto 2A** | Highest-value "no controls" Mode. Forces ghosting, program-dependent opto memory and SC emphasis. | F6, F9 (shelf), F12 (tube/transformer) |
| 5 | **M05 Mu 67** | The coupled TC selector, DC threshold→knee, progressive ratio and Lat/Vert. Hardest remap case, so do it while the descriptor is young. | F3, F7, F10 (M/S), F5 (TC5/6) |
| 6 | **M16 Diode 609** | Two-stage on a shared gain element, a1/a2 auto, stepped dBu thresholds, and the Slow attack coupled to the SC HPF (conditional dependency). | F11, F5, dependency machinery |
| 7 | **M11 Bus 25** | FF/FB switch at runtime, 3 knees, tilt emphasis, % link with shape, stepped ∪ variable release, auto makeup. | F4 (switchable), F8, F9 (tilt), F10 |
| 8 | **M30 Brickwall** | Fixes the latency, true-peak and oversampling architecture before more Modes depend on it. | F13, F14 |

**Next wave (Sprint 2–3):**
- M18 Octo (harmonic generator, 8 curves, 20 s opto): high value.
- M04 Opto Tube 1B (Fix/Man remap).
- M10 Console E.
- M06 Mu Mastering.
- M24 Twin Stage (serial stage 2, 3 transformers).
- M31 Pump (F15).
- M14 RMS 60 (rate ballistics, negative ratio).
- M13 Module Tilt, M12 Module FB.
- M19 Zener Desk (conditional lists).
- M17 Diode 54.
- M03 Opto 3A (a cheap variant of M02).

**Later (low value, or blocked by [U] data):**
- M20 Ruby 3, M21 Tri-Detector, M22 Bus Crush, M23 Bus Quad, M25 Class-A Master, M26 Negative Ratio.
- M27 Germanium 60, M07 Mu Broadcast, M08 Mu 36.
- M32–M35.

**Parallel-agent split for sprint 1.** Each Mode is a self-contained file (`modes/ModeXX*.h`: descriptor plus engine
config), and the shared engine blocks F1–F14 each have a separate owner.
1. Descriptor, parameter layout and UI binding: F2 and F3.
2. Gain computer, FB solver and characteristics-screen sampling: F1, F4, F7.
3. Detectors and ballistics: F5, F6, F8.
4. SC filters and linking: F9, F10.
5. Coloration and oversampling: F12, F14.
6. Look-ahead and latency: F13, F11.

---

## 7. Characteristics-screen implications (per family)

- **FF, static** (Clean, Bus G, Console E, Bus 25 New, RMS 60/65, Bus Crush, Bus Quad): evaluate the shared gain
  computer analytically over x ∈ [−60, 0] dBFS (B §7.5).
- **FB** (FET 76, Opto, Mu, Module FB, Diode, Bus 25 Old, Class-A Master FB): plot the fixed point `y = x − G(y)`. It
  converges in a few Newton steps for a monotone G. Show the *label* ratio next to the solved slope.
- **Program-dependent** (Opto 2A/3A, Mu 67 TC5/6, a1/a2, Auto, Anti-Log): the static curve is a steady-state
  approximation. Also show a **GR-vs-time** plot for a standard burst (for example a 10 dB step for 1 s, then silence).
  This makes the two-stage release visible. The telemetry "internals" (B §7.5) should expose the opto memory state, the
  fast and slow release envelopes, and the active TC.
- **Two-stage**: draw both stage curves and their combination (max-combine for shared-element, cascade for serial).
- **Coloration**: a harmonic readout (2nd/3rd level vs GR) is a natural "internals" panel for FET, Octo Dist2/3,
  transformers and Crush.

---

## 8. Open items (verify before implementing the affected Mode)

1. 1176: the knee shape per ratio button, the all-buttons static curve, and the detector (peak) implementation [U].
2. Fairchild TC4 attack: 0.8 ms (S3) or 0.4 ms (S4). Also whether the 670 has an output control.
3. SSL G bus: threshold range ±15 or ±20, the knee shape, the detector type, whether the side chain is a mono sum, and
   the Auto release constants.
4. SSL E: RMS vs peak detector [C S27].
5. API 525 release list [C S9]. API 2500 Thrust slopes [C S6 vs S7].
6. Neve 33609: compressor threshold and gain step sizes, limiter threshold steps, limiter recovery list and ratio. Neve
   2254: recovery list [C].
7. Distressor: detector HP frequency (80 or 100 Hz), the attack maximum (30 or 50 ms), the Brit-mode behaviour.
8. Shadow Hills: discrete attack and recovery intermediates, and the ratio list.
9. VSC-2: release list, SC HPF list, threshold range.
10. Smart C2: first attack step and release count.
11. Red 3, elysia alpha/mpressor, GML 8900, Pye 4060: numeric ranges (the hardware manuals are image-only or were not
    retrievable here).
12. Altec 436C: the ratio-vs-threshold law between the two published points.
13. Pro-C release range (for the digital Modes' host ranges).
