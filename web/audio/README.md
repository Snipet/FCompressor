# web/audio: the demo's sample loop

`loop.wav` is the SAMPLE LOOP of the browser demo (ADR-93): what START plays. The page fetches it as `audio/loop.wav`,
`web/sample.js` reads it and fits it to the audio context's rate as one exact period, and it plays at unity, looped,
with no fade. The other loop, the SYNTH LOOP, is made by the page (`web/loop.js`) and plays when this file does not
load.

Sean Funk made the loop for this demo on 2026-10-05. By its author's decision it is under the repository's licence,
the GNU GPL version 3 (`LICENSE`), like the rest of FCompressor.

## The file

- 2,048,600 bytes. SHA-256 `0327dec3cbc7de82cf3ed6d9f0533d7035681c20297b0c9cb7aae2bc9a8b7e52`.
- RIFF/WAVE with three chunks: `JUNK` (28 bytes), `fmt ` (16 bytes: PCM, 2 channels, 44100 Hz, 24 bits, 6 bytes a
  frame) and `data` (2,048,520 bytes).
- 341,420 frames: 7.742 s, 16 beats at 124 BPM.
- Peak -0.05 / -0.04 dBFS (left / right) and no sample at full scale. RMS -16.34 / -16.13 dBFS.
- Frame 0 is (0.005717, 0.009922) and a kick follows at once. The last 1 ms has an RMS of -71.9 dBFS. So the ends
  meet: the loop needs no fade, and a fade would take the first kick's attack.

## Nobody edits, converts or re-encodes it

The page plays these bytes, and the tests hold them: the file's SHA-256 is in `web/tests/sample.mjs` (web.sample),
`web/tests/page.mjs` (web.page, against the constant in `web/main.js`), `web/tests/size.mjs` (web.size, the site's
copy) and the page's own self-test (the row `sample.read`, on what the browser fetched). A file that differs by one
byte fails all four. The facts above are the contract in `docs/sprints/web-loop.md`.

The site gets `web/audio/*.wav` as `audio/` (`cmake/FcmpWebSite.cmake`). This README stays in the repository.
