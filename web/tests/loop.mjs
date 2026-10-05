// web/tests/loop.mjs: the demo's synth loop, as the page makes it (ADR-93, web Sprint D).
//
// FCMP_WEB_TEST name=web.loop timeout=60 args={source}/web/loop.js
//
//   node loop.mjs <loop.js>
//
// web/loop.js is imported as it is and synthLoop() is called at 44.1, 48 and 96 kHz. The rows hold the loop's
// properties, never a hash: Math.sin and Math.exp differ in the last bit between JavaScript engines, so node's
// samples are not a browser's.
//   length       16 beats of a whole number of frames each, 100 BPM (9.6 s), two channels of that length
//   finite       no NaN, no infinity
//   peak         the largest sample is -3 dBFS (within 0.1 dB)
//   rms          between -16 and -13 dBFS over both channels
//   dc           each channel's mean is below 1e-4
//   seam         the step from the last frame to the first is no larger than the steps before it (the loop's own
//                slope there, within a half), and below 1 % of the largest step inside the loop: no click
//   transients   50 ms windows range over at least 6 dB (the drums), and the crest factor is at least 10 dB
//   sustain      no 50 ms window is below -30 dBFS (the bass and the pad never stop)
// and once, at 48 kHz: the same samples from a second call (no hidden state), and two channels that differ.
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { pathToFileURL } from 'node:url';

const TEST = 'web.loop';
const RATES = [44100, 48000, 96000];
const BEATS = 16;
const BPM = 100;

let passed = 0;
let failed = 0;
function row(ok, name, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}     ${TEST} ${name}${detail ? ': ' + detail : ''}`);
  if (ok) passed += 1;
  else failed += 1;
  return ok;
}
function note(text) {
  console.log(`NOTE     ${text}`);
}

const [loopPath] = process.argv.slice(2);
if (!loopPath) {
  console.error('usage: node loop.mjs <loop.js>');
  process.exit(2);
}
const { synthLoop } = await import(pathToFileURL(loopPath).href);
row(typeof synthLoop === 'function', 'exports', 'synthLoop(sampleRate)');
if (typeof synthLoop !== 'function') process.exit(1);

const db = (v) => 20 * Math.log10(v);
for (const rate of RATES) {
  const t0 = performance.now();
  const loop = synthLoop(rate);
  const ms = performance.now() - t0;
  const { left: L, right: R, frames: n } = loop;
  const beat = n / BEATS;
  row(L instanceof Float32Array && R instanceof Float32Array && L.length === n && R.length === n
      && loop.sampleRate === rate && Number.isInteger(beat) && beat === Math.round(rate * 60 / BPM), `length.${rate}`,
      `${n} frames: ${BEATS} beats of ${beat} (${(n / rate).toFixed(3)} s), made in ${ms.toFixed(0)} ms`);

  let finite = true;
  let peak = 0;
  let squares = 0;
  let sumL = 0;
  let sumR = 0;
  let largestStep = 0;
  for (let i = 0; i < n; i += 1) {
    finite = finite && Number.isFinite(L[i]) && Number.isFinite(R[i]);
    peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
    squares += L[i] * L[i] + R[i] * R[i];
    sumL += L[i];
    sumR += R[i];
    if (i + 1 < n) largestStep = Math.max(largestStep, Math.abs(L[i + 1] - L[i]), Math.abs(R[i + 1] - R[i]));
  }
  const rms = Math.sqrt(squares / (2 * n));
  row(finite, `finite.${rate}`);
  row(Math.abs(db(peak) + 3) <= 0.1, `peak.${rate}`, `${db(peak).toFixed(2)} dBFS`);
  row(db(rms) >= -16 && db(rms) <= -13, `rms.${rate}`, `${db(rms).toFixed(2)} dBFS (the window is -16 to -13)`);
  row(Math.abs(sumL / n) < 1e-4 && Math.abs(sumR / n) < 1e-4, `dc.${rate}`,
      `means ${(sumL / n).toExponential(1)} and ${(sumR / n).toExponential(1)}`);

  const seam = Math.max(Math.abs(L[0] - L[n - 1]), Math.abs(R[0] - R[n - 1]));
  let before = 0;
  for (let i = n - 65; i < n - 1; i += 1) {
    before = Math.max(before, Math.abs(L[i + 1] - L[i]), Math.abs(R[i + 1] - R[i]));
  }
  row(seam <= 1.5 * before && seam < 0.01 * largestStep, `seam.${rate}`,
      `a step of ${seam.toExponential(2)} from the last frame to the first; the 64 steps before it reach `
      + `${before.toExponential(2)}, the largest in the loop is ${largestStep.toFixed(3)}`);

  const window = Math.round(0.05 * rate);
  let quietest = Infinity;
  let loudest = 0;
  for (let at = 0; at + window <= n; at += window) {
    let s = 0;
    for (let i = at; i < at + window; i += 1) s += L[i] * L[i] + R[i] * R[i];
    s = Math.sqrt(s / (2 * window));
    quietest = Math.min(quietest, s);
    loudest = Math.max(loudest, s);
  }
  row(db(loudest) - db(quietest) >= 6 && db(peak) - db(rms) >= 10, `transients.${rate}`,
      `50 ms windows from ${db(quietest).toFixed(1)} to ${db(loudest).toFixed(1)} dBFS, `
      + `crest ${(db(peak) - db(rms)).toFixed(1)} dB`);
  row(db(quietest) >= -30, `sustain.${rate}`, `the quietest 50 ms is ${db(quietest).toFixed(1)} dBFS`);
}

{
  const a = synthLoop(48000);
  const b = synthLoop(48000);
  let same = a.frames === b.frames;
  let apart = 0;
  for (let i = 0; i < a.frames && same; i += 1) {
    same = Object.is(a.left[i], b.left[i]) && Object.is(a.right[i], b.right[i]);
    apart = Math.max(apart, Math.abs(a.left[i] - a.right[i]));
  }
  row(same, 'deterministic', 'a second call gives the same samples');
  row(apart > 0.01, 'stereo', `the channels differ by up to ${apart.toFixed(3)}`);
}
note('the samples are this JavaScript engine\'s: a browser\'s differ in the last bits, so no row is a hash');

console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
