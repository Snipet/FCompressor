# Web demo: the sample loop — task manifests

The demo gets a loop that was made for it (the user's file, 2026-10-05) beside the loop the page synthesises. One
worktree (`/Users/seanfunk/audio/plugins/FCompressor.wt/sample-loop`), one branch (`web/sample-loop`), base `main`
cc34611. The cards run one after another in that worktree (W-G and W-D may run side by side: their files are apart).
Each card ends with ONE handoff commit on `web/sample-loop`; the lead reviews, runs the gates, opens the pull request
and merges. The rules of `CLAUDE.md` hold: no push, no bless, no network, no build outside this worktree.

Ownership in a shared branch: note `git rev-parse HEAD` when you start; when you finish, every path of
`git diff --name-only <that sha>` plus `git status --short` must match your OWNS.

## The loop's contract (frozen: every card writes against it)

- **The file**: `web/audio/loop.wav`, the user's file byte for byte. 2,048,600 bytes, sha256
  `0327dec3cbc7de82cf3ed6d9f0533d7035681c20297b0c9cb7aae2bc9a8b7e52`. RIFF/WAVE with three chunks: `JUNK` (28 bytes),
  `fmt ` (16 bytes: tag 1, 2 channels, 44100 Hz, 24 bits, block 6), `data` (2,048,520 bytes: 341,420 frames, 7.742 s,
  16 beats at 124.000 BPM). Measured by the lead: peak -0.05 / -0.04 dBFS (left / right, frames 277521 / 256096), RMS
  -16.34 / -16.13 dBFS, mean -6.7e-4 / -5.8e-4, no sample at full scale; frame 0 is (0.005717, 0.009922) and a kick
  follows at once (the first 1 ms peaks at -1.7 dBFS); the last 1 ms has an RMS of -71.9 dBFS. So the ends meet: the
  loop needs no fade, and a fade would take the first kick's attack. In the site it is `audio/loop.wav`. Nobody edits,
  converts or re-encodes it.
- **`web/sample.js`**: an ES module with no DOM, no `fetch` and no clock in it (it runs under node as it is; the page
  imports it as `./sample.js`).
  - `export const SAMPLE_URL = 'audio/loop.wav';`
  - `export function readWav(bytes)`: `bytes` is an ArrayBuffer or a typed-array view of the file. Returns
    `{ sampleRate, frames, left, right }` with two Float32Arrays of `frames` samples. It reads RIFF/WAVE: integer PCM
    of 16, 24 and 32 bits and 32-bit float; format tags 1, 3 and 0xFFFE (the sub-format's first two bytes are the
    tag); one channel (both sides get it) or two; chunks in any order, the pad byte after an odd chunk, chunks it does
    not know passed over. An integer sample `s` of `b` bits is `s / 2^(b-1)`. Anything else it refuses by throwing an
    `Error` whose message says what is wrong in plain words (not RIFF/WAVE, no `fmt ` before use, no `data`, a format
    it does not read, a channel count it does not read, no whole frame, a chunk that runs past the end of the bytes:
    a download cut short is refused, never played short).
  - `export function fitLoop(loop, sampleRate)`: the loop as ONE PERIOD at another rate. `loop` is what `readWav`
    returns (or `synthLoop`); the result has the same shape with `sampleRate` the wanted rate and
    `frames = Math.round(loop.frames * sampleRate / loop.sampleRate)`. The input is taken as periodic (index modulo
    `frames`), and the ratio is exactly `frames out / frames in`, so the result is itself one exact period: it loops
    with no seam and needs no fade. When the rates are equal it returns `loop` itself. Windowed sinc, low-passed at
    the lower of the two rates; deterministic (no `Math.random`, no time). What it must hold is in W-A's acceptance.
- **Why the page does this itself**: START asks for a 48 kHz context, so the 44.1 kHz file is resampled for nearly
  every visitor. `decodeAudioData` resamples a file as a one-shot (silence before and after), which is a seam every
  7.7 s when it loops, and differs by browser; a buffer source at another rate than the context's is interpolated
  linearly in one engine. The page's own periodic resampling is the same in every browser, and node can test it.

## W-A

```text
TASK    W-A   card: WEB-LOOP.1   repo: FCompressor   size: M
        worktree: /Users/seanfunk/audio/plugins/FCompressor.wt/sample-loop   branch: web/sample-loop (as it is)
GOAL    web/sample.js reads the sample loop and fits it to any context rate as an exact period; a node test holds it
OWNS    web/sample.js web/tests/sample.mjs
FROZEN  "The loop's contract" in this file; web/audio/loop.wav; every other path. Never bless.
FUNKGUI the pin. No FunkGui change in this card.
DONE    03 §4.7 where it applies (ownership clean; the tests pass; <= 40-line handoff) plus the Acceptance below.
READS   web/loop.js and web/tests/loop.mjs (the house form of a page module and of its node test: the header
        comment, the row() and note() helpers, PASS/FAIL/NOTE lines, exit 0/1/2) cmake/FcmpWeb.cmake (the
        FCMP_WEB_TEST line: lines 40-70 and 235-260) web/tests/size.mjs (lines 1-60: how a test's header reads)
INPUTS  web/audio/loop.wav (already in the worktree, committed by the lead)
DELIVERABLES
        1. web/sample.js to the contract. A header comment in the form of web/loop.js: what the file is, the exports
           with one example of use, why the page resamples by itself, the filter's design in numbers (taps, window,
           cut-off, table), what it costs. Plain English, short sentences.
           fitLoop: a Kaiser-windowed sinc from a table (the kernel sampled finely, read with linear interpolation;
           one set of weights per output frame, used for both channels, summed in double precision). The position of
           output frame k is k * framesIn / framesOut input frames, taken exactly (the integer part and the
           remainder from integers: no drift over 370,000 frames). The cut-off is below half the LOWER rate, the
           kernel widens when the rate goes down. Each frame's weights are normalised to sum to 1.
        2. web/tests/sample.mjs, `// FCMP_WEB_TEST name=web.sample timeout=120 on=web
           args={source}/web/sample.js,{source}/web/audio/loop.wav`. Rows (name them as web/tests/loop.mjs names its):
           wav.<case>     readWav on WAVs the test builds in memory: 16, 24 and 32-bit PCM, 32-bit float, the
                          extensible tag, mono, an odd chunk with its pad byte, a chunk after `data`, `data` before
                          a chunk it does not know: every sample exactly what was written. And each refusal with a
                          message that names the fault: not RIFF, not WAVE, no fmt, no data, tag 2, 8 bits, three
                          channels, no frame, a data chunk longer than the bytes, a file cut in the middle of a
                          chunk header.
           file.<fact>    web/audio/loop.wav: its size and sha256 (node:crypto) as the contract gives them; 44100 Hz,
                          341,420 frames; each side's peak below 0 dBFS and no sample at +-1; RMS between -17.5 and
                          -15 dBFS; |mean| under 0.002; the last 1 ms under -60 dBFS RMS; |frame 0| under 0.02.
           fit.same       at the loop's own rate fitLoop returns the same object.
           fit.frames     the sample loop at 48000 has 371,614 frames, at 96000 743,227, at 24000 185,807, at 44100
                          341,420; frames is always Math.round(framesIn * rate / rateIn).
           fit.period     a loop made of sines with whole numbers of cycles per loop (several, from 30 Hz to 20 kHz
                          at 44100 Hz, unequal phases, different on the two sides) fitted to 48000 and to 96000 is
                          that same sum of sines at the new rate: the error's RMS at most -100 dB under the
                          signal's, the worst single sample at most -90 dB under the signal's peak, and the same
                          bounds hold in the first and last 64 frames (the seam). A loop of 341,420 frames is one
                          case, a short one (under 3,000 frames, still longer than the kernel)
                          another; and a loop SHORTER than the kernel (64 frames) must still be right (the
                          modulo wraps more than once).
           fit.passband   44100 to 48000: single sines of whole cycles at 20, 100, 1000, 5000, 10000, 15000, 18000
                          and 20000 Hz keep their level within 0.01 dB (0.1 dB at 20000) and their phase (the
                          filter is linear phase with no delay: the error bound of fit.period says so too).
           fit.images     44100 to 48000 with a sine at 15000 and one at 20000 Hz: everything in the output that is
                          not the sine (the error against the analytic sine) is at most -100 dB under it.
           fit.down       44100 to 24000: a sine at 5000 Hz comes out within 0.01 dB; a sine at 14000 Hz and one at
                          20000 Hz (above the new half rate) leave at most -90 dB of anything. And 44100 to 16000
                          the same with 3000 Hz (kept) and 10000 Hz (removed).
           fit.dc         a constant loop comes out as the same constant to 1e-6 at every tested rate.
           fit.sample     the sample loop at 48000: its RMS within 0.01 dB of the file's on each side; the last
                          1 ms under -60 dBFS; |frame 0| under 0.05; fitted twice, the same bits. A NOTE with its
                          largest sample in dBFS at 48000 and at 96000 (resampling raises peaks: the lead wants the
                          number), and a NOTE with the milliseconds fitLoop took at 48000 (a row only that it is
                          under 5 s: a time is no gate on a loaded machine).
        ACCEPTANCE (part of DONE)
        [NODE]  cd "$WT" && node web/tests/sample.mjs web/sample.js web/audio/loop.wav: exit 0, every row PASS.
                Show two of the rows FAILING on a deliberately wrong sample.js (for example: no modulo at the ends;
                the cut-off at half the higher rate when the rate goes down), then restore it.
        [WEB]   cd "$WT" && FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify && Scripts/verify.sh --strict
                "$WT/build-web": the new test is found and passes as web.sample. web.size FAILS on this card's tree
                (the site does not carry sample.js's budget yet, and web/audio is not in the site): that one
                failure, `files`/`size`, belongs to card W-P and is expected; say so, and report every other
                failure as yours.
        [TIME]  fitLoop at 48000 under node on this machine: aim for under 400 ms; say what it is.
NOTES   No dependency, no package. Float32Array out, Float64 inside. Do not touch web/main.js or the site script:
        the page card wires the module in. web/loop.js stays as it is.
```
