# Web demo: the sample loop — task manifests

The demo gets a loop that was made for it (the user's file, 2026-10-05) beside the loop the page synthesises. One
worktree (`/Users/seanfunk/audio/plugins/FCompressor.wt/sample-loop`), one branch (`web/sample-loop`), base `main`
cc34611. W-A, W-P and W-G run one after another in that worktree, each ending with ONE handoff commit on
`web/sample-loop`; W-D (the documents) runs beside W-G in a worktree of its own (`…/FCompressor.wt/sample-loop-docs`,
branch `web/sample-loop-docs`, made by the lead from W-P's commit). The lead reviews, merges the two, runs the gates,
opens the pull request and merges it. The rules of `CLAUDE.md` hold: no push, no bless, no network, no build outside
your worktree.

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

## The page (frozen with the contract above; the user's decisions of 2026-10-05 are marked)

- **Names**: the file is the SAMPLE LOOP; the loop the page synthesises (`web/loop.js`) is the SYNTH LOOP. The words
  BUILT-IN LOOP go, everywhere.
- **The default** (the user's decision): START plays the sample loop. The synth loop is the second choice and what
  plays when the file does not load.
- **`#fcmp-source`** holds, in this order: `#fcmp-source-name`; the buttons `#fcmp-loop` (SAMPLE LOOP), `#fcmp-synth`
  (SYNTH LOOP) and `#fcmp-open` (OPEN AN AUDIO FILE); the file input; the sentence about dropping a file;
  `#fcmp-notice`. `#fcmp-source-name` says `SOURCE: SAMPLE LOOP` in index.html (before START) and then what plays:
  `SOURCE: SAMPLE LOOP`, `SOURCE: SYNTH LOOP` or `SOURCE: <FILE NAME>`. The three buttons are disabled before START
  and once the demo has failed or stopped. While it runs `#fcmp-open` is enabled; of the two loop buttons the one
  whose loop plays is disabled and the other enabled; and `#fcmp-loop` is enabled whenever the sample loop is not
  loaded (a press tries again).
- **START** loads the sample loop beside the worklet and the engine: `fetch(SAMPLE_URL)` (relative, as every URL of
  the page), `readWav`, `fitLoop(…, context.sampleRate)`, an AudioBuffer of exactly the fitted frames. It plays at
  unity through the same gain node as any source, looped, with NO fade: the page never changes the file's level, and
  the fitted loop is its own exact period. When the response is not ok, the fetch rejects, `readWav` throws, or the
  loop is not ready `SAMPLE_MS` (15000) after its fetch began (the fetch is then aborted), START plays the synth loop
  and the notice says `SAY.sampleLost`: `THE SAMPLE LOOP DID NOT LOAD. THE SYNTH LOOP PLAYS INSTEAD.` A sample loop
  that fails never fails START. The synth loop is made when it is first needed, not at every START.
- **`#fcmp-loop`**, while the demo runs: the sample loop plays (the 30 ms swap of today). When it is not loaded the
  page loads it first (the notice says `SAY.sampleLoading`: `LOADING THE SAMPLE LOOP.`), plays it and clears the
  notice; when that fails the notice says `SAY.sampleUnchanged`: `THE SAMPLE LOOP DID NOT LOAD. THE SOURCE IS
  UNCHANGED.` A later choice (the other button, a file) wins over a load that is still running: the `opening` turn.
- **`#fcmp-synth`**, while the demo runs: the synth loop plays and the notice is cleared.
- **A file**: as today (decoded by the browser, 5 ms fades at both ends).
- **What the review of W-P added** (commit baa06c2; frozen with the rest): a retry of the sample loop asks the server
  and not the cache (`cache: 'reload'`; START's own fetch is the plain one); a file chosen or dropped while START is
  still loading is kept and plays once the demo runs (it wins over the sample loop); a failure of the engine or the
  editor while START loads ends the load at once (not after `SAMPLE_MS`); when the demo ends (failed or stopped) the
  notice is cleared and a load that runs is given up; a context that is closed from outside stops the demo; two
  presses under 30 ms apart never let the source between them reach the engine un-faded.
- **`globalThis.fcmpPage.source()`**: `null` until a source plays, then `{ kind, name, frames, sampleRate }` of the
  buffer that plays: `kind` is `'sample'`, `'synth'` or `'file'`, `name` what the page says after `SOURCE: `.
- **The self-test** gains three rows: 15 with a context that runs and 14 with one that is suspended (it had 12 and
  11; the lead first wrote thirteen here, which was wrong). The first two come after `engine.silence` and before
  `editor.*`:
  `sample.read` (the page's own `fetch(SAMPLE_URL)`: the SHA-256 of the bytes, by `crypto.subtle`, is `SAMPLE_SHA256`,
  a constant main.js exports with the contract's value; `readWav` gives 44100 Hz and 341,420 frames),
  `sample.fit` (`fitLoop` to 48000: 371,614 frames, each side's RMS within 0.01 dB of the file's, the last 1 ms under
  -60 dBFS; a NOTE with the milliseconds it took), and, after `page.start`,
  `page.source` (`fcmpPage.source()` is the sample loop at the context's rate with
  `Math.round(341420 * rate / 44100)` frames; the name line says `SOURCE: SAMPLE LOOP`; the notice is empty;
  `#fcmp-loop` is disabled and `#fcmp-synth` enabled). `worklet.render` and `engine.silence` keep the synth loop: at
  48 kHz it is a whole number of quanta, the sample loop is not.
- **The footer** (the user's decision: the loop is under the repository's licence): its first sentence becomes
  `FCOMPRESSOR IS FREE SOFTWARE UNDER THE GNU GPL VERSION 3, AND SO IS THE SAMPLE LOOP, WHICH SEAN FUNK MADE FOR THIS
  DEMO.` (the link as today, on GNU GPL VERSION 3).
- **The site** has 15 files: the 13 of today, `sample.js` and `audio/loop.wav`. The site script copies
  `web/audio/*.wav` into `audio/` and nothing else of `web/audio/`. `web/audio/README.md` (what the file is, its
  facts from the contract, who made it and when, the licence) stays in the repository.
- **The gate's new rows** (W-G): the scripted user's `source.sample`, `source.synth`, `source.back`, `source.lost`
  and `source.again`; the published-site form's `audio` (the published `audio/loop.wav` has the contract's size and
  SHA-256), after `published`.

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
                          largest sample in dBFS at 48000 and at 96000 (resampling can raise peaks: the lead wants the
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

## W-P

```text
TASK    W-P   card: WEB-LOOP.2   repo: FCompressor   size: L
        worktree: /Users/seanfunk/audio/plugins/FCompressor.wt/sample-loop   branch: web/sample-loop (as it is)
GOAL    The page plays the sample loop by default, offers the synth loop, and survives a sample loop that is missing
OWNS    web/main.js web/index.html web/demo.css web/tests/page.mjs web/tests/size.mjs cmake/FcmpWebSite.cmake
        web/audio/README.md
FROZEN  "The loop's contract" and "The page" in this file; web/sample.js (W-A's: if it must change, say so in the
        handoff and change nothing); web/loop.js, web/fcmp-worklet.js, web/audio/loop.wav; the other tests of
        web/tests; Scripts/** (the gate is W-G's); docs (W-D's). Never bless.
FUNKGUI the pin. No FunkGui change in this card.
DONE    03 §4.7 plus the Acceptance below.
READS   this file (all of it) web/main.js (all of it: the header comment lists what the page does and must stay true)
        web/index.html web/demo.css web/sample.js web/loop.js web/tests/page.mjs web/tests/size.mjs
        web/tests/site.mjs cmake/FcmpWebSite.cmake docs/DECISIONS.md (ADR-93: search "The page" and "The site")
        Scripts/web/scenario/file.mjs and start.mjs (what the gate reads of the page today: W-G restates them)
INPUTS  W-A's commit on this branch (web/sample.js, web/tests/sample.mjs)
DELIVERABLES
        1. web/main.js, web/index.html, web/demo.css to "The page". Keep every rule the file states of itself: the
           context is made and resumed inside the click, before the first await; `wanted()` is asked after every
           await; nothing the sample loop does can throw out of start(); every URL relative; what the page says is
           upper case and comes from SAY; the pieces with no browser in them are exported and tested under node
           (the decision "which buttons are enabled" is one such piece: a pure function of what plays and whether
           the sample loop is loaded). fitLoop blocks the main thread for its time (W-A's handoff has the number):
           place it where it delays no gesture rule and say in a comment what it costs. The header comment, the
           comment in index.html's inline script (it names loop.js) and the SAY table are brought up to date.
        2. web/tests/page.mjs: rows for the new exported pieces (the texts, SAMPLE_SHA256 equal to the SHA-256 of
           web/audio/loop.wav, the buttons' rule in every state, the page's files list with sample.js), and the
           rows that read index.html hold the new controls, their order and the footer's sentence.
        3. cmake/FcmpWebSite.cmake: `audio/loop.wav` in the site (a missing web/audio/loop.wav fails the script, as
           a missing index.html does); the header comment's list. web/tests/size.mjs: the table measured again (15
           files; the "measured now" lines), a row `audio.loop` (the site's copy has the contract's size and
           SHA-256: this file never grows by 20 %), and the header says when it was measured.
        4. web/audio/README.md: short. What the file is and is for; its facts (from the contract); made by Sean
           Funk for this demo, 2026-10-05; under the repository's licence, GPL-3.0 (LICENSE), the user's decision;
           that nobody edits or re-encodes it and why (the page reads these bytes; the SHA-256 is held by tests).
        ACCEPTANCE (part of DONE)
        [WEB]    cd "$WT" && FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify && Scripts/verify.sh --strict
                 "$WT/build-web": exit 0 (web.size, web.page, web.site, web.sample and the rest).
        [CHROME] headless Chrome, always --mute-audio, your own throwaway --user-data-dir under your scratch, the
                 site served from 127.0.0.1 by a server of yours (python3 -m http.server is enough), driven by a
                 scratch script of yours (Scripts/web/cdp.mjs may be imported read-only). Show, with what
                 fcmpPage.source() and the page said each time:
                 a. START: the sample loop plays (kind sample, 371,614 frames at 48000), the name line, the buttons;
                 b. SYNTH LOOP, then SAMPLE LOOP: each plays and the buttons swap;
                 c. a site copy WITHOUT audio/loop.wav: START plays the synth loop with SAY.sampleLost, #fcmp-loop
                    stays enabled; a press says SAY.sampleUnchanged; the file put back, a press plays the sample
                    loop and clears the notice;
                 d. a site copy whose audio/loop.wav is cut to half its bytes: as c (readWav refuses it);
                 e. a file dropped while the sample loop is still loading wins;
                 f. index.html?selftest=1 with autoplay allowed (--autoplay-policy=no-user-gesture-required): the
                    title is PASS and the log has the 15 rows; and once without autoplay (14).
                 Chrome plays through the user's speakers unless muted: never start it without --mute-audio.
        [GATE]   Do NOT fix the gate. Run cd "$WT" && Scripts/web-live.sh "$WT/build-web" once at the end and list
                 the rows that fail now (they name the old words or the old default): that list is W-G's input. A
                 row that fails for another reason is yours.
        [SIZES]  The site's bytes and gzip bytes before and after (web.size's NOTE lines), and START's time to
                 PLAYING on this machine before and after (from the click to state running; say how measured).
NOTES   The self-test's `within(20000, 'the start', start())` must still hold when the file is missing or slow:
        SAMPLE_MS is below it on purpose. Do not add a query parameter for tests: the gate presses the buttons.
```

## W-G

```text
TASK    W-G   card: WEB-LOOP.3   repo: FCompressor   size: L
        worktree: /Users/seanfunk/audio/plugins/FCompressor.wt/sample-loop   branch: web/sample-loop (as it is)
GOAL    The browser gate holds the new default source, the two loop buttons and the fallback, with a mutant for each
OWNS    Scripts/web/scenario.mjs Scripts/web/scenario/*.mjs Scripts/web/live.mjs Scripts/web/page-check.mjs
        Scripts/web/cdp.mjs (only to add what a row needs, such as blocking one URL; no exported name changes)
        Scripts/web-live.sh (only its usage text and comments, if a row count is written there)
        web/tests/weblive.mjs web/tests/pagecheck.mjs web/tests/support/*.mjs
FROZEN  "The loop's contract" and "The page" in this file; web/main.js, web/index.html, web/sample.js and the rest
        of the page (W-P's: a fault you find there goes in the handoff, with the row that shows it). Never bless.
FUNKGUI the pin. No FunkGui change in this card.
DONE    03 §4.7 plus the Acceptance below.
READS   this file (all of it) W-P's handoff commit (git show --stat, and the page's files) Scripts/web/scenario.mjs
        (its header lists every row) Scripts/web/scenario/*.mjs (all: driver.mjs, file.mjs, start.mjs, mutants.mjs
        first) Scripts/web/live.mjs (the header, the published form, how rows are counted) Scripts/web/page-check.mjs
        web/tests/weblive.mjs web/tests/pagecheck.mjs docs/design/03-build-verify-process.md §3.6
INPUTS  W-A's and W-P's commits on this branch; W-P's list of the gate rows that fail on its tree.
DELIVERABLES
        1. Every row of the scripted user that named the old words or relied on the old default is restated so that
           it proves what it proved: a row that told a dropped file from "the built-in loop" by level (the synth
           loop never passes -3 dBFS; the sample loop's largest sample is -0.04 dBFS in the file and -0.05 dBFS
           fitted to 48 kHz: W-A measured that no sample reaches full scale at any rate) must tell them apart some
           other way that cannot pass by accident (fcmpPage.source() and what the engine's input meter shows
           together, for example).
        2. The new rows of "The page": source.sample (after START: the page says SAMPLE LOOP, fcmpPage.source() is
           kind sample with Math.round(341420 * rate / 44100) frames, and the engine's input IS that loop: its
           input level reaches above -1.5 dBFS within one loop, which the synth loop never does), source.synth
           (SYNTH LOOP pressed: the page, source() and the input level, which stays under -2.5 dBFS for a whole
           loop), source.back (SAMPLE LOOP pressed: back, the notice empty), source.lost (audio/loop.wav
           unreachable: START still reaches PLAYING, the synth loop plays, the notice is SAY.sampleLost and
           #fcmp-loop stays enabled) and source.again (reachable again: a press plays the sample loop and clears
           the notice; while still unreachable a press says SAY.sampleUnchanged and the source stays). Where a row
           needs the file unreachable, block that one URL in the browser (the DevTools protocol can) or serve a
           site without it: the shipped site is never changed in place.
        3. mutants.mjs: the old mutant of BUILT-IN LOOP becomes one per loop button, and each new behaviour has a
           mutant that exactly the rows meant for it catch: START never asks for the sample loop; the page plays
           the file's frames without fitLoop (341,420 frames at 48000); the failure is swallowed without a notice;
           a failed sample loop fails START; #fcmp-loop disabled after a failed load; the notice not cleared when
           the sample loop plays again; SYNTH LOOP plays the sample loop. A mutant that no row catches is a row to
           write, not a mutant to drop.
        4. live.mjs: the published form gets the row `audio` after `published` (the published audio/loop.wav: the
           contract's size and SHA-256; when it fails the pages still run). Row counts in comments, in usage texts
           and in web/tests/weblive.mjs (the runner against its fakes) follow. page-check.mjs and its test: whatever
           counts or names the self-test's rows or the page's controls follows (15 rows, 14 suspended).
        ACCEPTANCE (part of DONE)
        [GATE]    cd "$WT" && Scripts/web-live.sh "$WT/build-web": exit 0, every row passed; give the row count
                  and the scripted user's row count (79 before this card, 81 with --png).
        [MUTANTS] the scripted user's mutant run (as its header says to run it): every mutant caught, by the rows
                  meant for it; the table in the handoff.
        [WEB]     cd "$WT" && FCMP_TIMING_SCALE=3 cmake --workflow --preset web-verify && Scripts/verify.sh
                  --strict "$WT/build-web": exit 0 (web.weblive and web.pagecheck among them). lint.docs is W-D's.
        [URL]     no network for you: the published form's new row is proved against the runner's fakes
                  (web/tests/weblive.mjs), with a wrong size and a wrong hash each failing the row.
NOTES   The gate starts headless Chrome muted with its own profile, by itself: do not start another browser. One
        gate run at a time. Results go under the build directory, never into the repository.
```

## W-D

```text
TASK    W-D   card: WEB-LOOP.4   repo: FCompressor   size: M
        worktree: /Users/seanfunk/audio/plugins/FCompressor.wt/sample-loop-docs   branch: web/sample-loop-docs
GOAL    The documents say what the demo now plays, where it is served, and what was measured
OWNS    README.md docs/DECISIONS.md (ADR-93 only) docs/ARCHITECTURE.md docs/design/03-build-verify-process.md
        docs/design/01-core-contracts.md (only where it names the page's files) CLAUDE.md (only if a rule there
        names the page's files or the site) wrangler.jsonc (comments only: no key changes)
FROZEN  Everything else. No code, no test, no script. The contract and "The page" in this file are the facts.
FUNKGUI the pin. No FunkGui change in this card.
DONE    Every added statement is true and has its source (a file, a commit, a number in a handoff or in THE LEAD'S
        FACTS below); cmake -DFCMP_SOURCE_DIR="$WT" -P cmake/LintDocs.cmake passes; no line over 120 columns.
READS   this file (all of it) the handoffs of W-A and W-P (the lead's prompt gives them) README.md ("Web demo",
        "Licence") docs/DECISIONS.md ADR-93 (all of it: it is long; keep its structure) docs/ARCHITECTURE.md §3.1
        docs/design/03-build-verify-process.md §2.12, §3.6, §4.10 wrangler.jsonc cmake/LintDocs.cmake
DELIVERABLES
        1. ADR-93: the sample loop, where the page and the site are described. What it is and who made it (the
           user's file of 2026-10-05, under the repository's licence by the user's decision); the file's facts;
           why it is a WAV as given (as FLAC it measured 1,589,157 bytes, 78 % of the WAV: not worth a decoder the
           node tests cannot run; gzip takes 4 %); why the page reads and resamples it by itself, with W-A's
           numbers (filter, errors, time, the largest sample after fitting); the default and the fallback; the
           self-test's 15 rows (14 suspended); the site's 15 files and bytes (W-P's numbers); the gate's new rows
           (the names in "The page"). "No audio file is in the repository" and every count of 13 files that speaks of
           the present are brought up to date; a sentence that reports a past measurement (a deploy, a CI run) stays as
           history. The list of what is left: "recorded loops" is done with one loop; more would need a selector.
        2. ADR-93, "A second host", and the README: the Cloudflare copy is served at the user's own domain,
           https://fcompressor.seanfunk.xyz, as well as at its workers.dev address. THE LEAD'S FACTS: the user
           attached the domain in the Cloudflare dashboard and said so on 2026-10-05; wrangler.jsonc has no
           `routes`, and wrangler publishes custom domains only when the configuration lists some (read in the
           source of wrangler 4.100.0, `triggersDeploy`: `if (customDomainsOnly.length > 0)`), so a deploy leaves
           a domain attached in the dashboard as it is; on 2026-10-05 the three addresses served `site cc34611…
           clean`, and the lead's run of Scripts/web-live.sh --url against the domain is in the prompt.
        3. README "Web demo": the addresses (the user's domain first), what plays (the sample loop, made for the
           demo by Sean Funk; the synth loop as the other choice), and in "Licence" that the loop is under the
           GPL as well. Keep the section's length close to what it is.
        4. docs/ARCHITECTURE.md §3.1 and 03 §2.12: the page's files (sample.js, audio/loop.wav), the site's 15
           files; 03 §3.6: the gate's new rows and the published form's `audio` row.
        5. wrangler.jsonc: the comment that says the Worker is served at its workers.dev address "and nowhere else"
           is no longer true: say where it is served and why the configuration lists no route (item 2).
NOTES   Plain English, short sentences, the documents' own voice. Counts that W-G decides (the gate's total rows)
        are not yours to guess: write the rows' names, and leave totals that you cannot source out. No maker names.
```
