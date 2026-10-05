// web/tests/sample.mjs: the demo's sample loop: its file, the WAV reader and the fit to another rate (ADR-93, the
// sample loop: docs/sprints/web-loop.md).
//
// FCMP_WEB_TEST name=web.sample timeout=120 on=web args={source}/web/sample.js,{source}/web/audio/loop.wav
//
//   node sample.mjs <sample.js> <loop.wav>
//
// web/sample.js is imported as it is. The rows:
//   exports             SAMPLE_URL, readWav(bytes) and fitLoop(loop, sampleRate)
//   wav.<case>          readWav on files this test builds in memory, every sample exactly what was written: PCM of
//                       16, 24 and 32 bits, 32-bit float, the extensible tag over both, one channel, an odd chunk
//                       with its pad byte, a chunk after `data`, `data` before a chunk it does not know and before
//                       `fmt `, the file as a view into a larger buffer, bytes after the RIFF chunk
//   wav.refuse.<fault>  each file readWav must refuse, by an Error whose message names the fault: not RIFF, not WAVE,
//                       too short to be either, no fmt, no data, tag 2, 8 bits, 64-bit float, three channels, a rate
//                       of 0, a frame size that is not the channels' and the bits', an extensible tag with no
//                       sub-format, no whole frame, a data chunk longer than the bytes, a file cut in the middle of a
//                       chunk header (both cuts twice: as a download cut short, and with a RIFF size that agrees)
//   file.<fact>         web/audio/loop.wav: its size and its sha256; 44100 Hz and 341,420 frames; each side's peak
//                       below 0 dBFS with no sample at +-1; each RMS between -17.5 and -15 dBFS; each |mean| under
//                       0.002; the last 1 ms under -60 dBFS RMS; |frame 0| under 0.02
//   fit.sample.<fact>   the sample loop at 48000: each side's RMS within 0.01 dB of the file's; the last 1 ms under
//                       -60 dBFS; |frame 0| under 0.05; fitted twice, the same bits; fitted in under 5 s (a time is
//                       no gate on a loaded machine: the NOTE has the milliseconds)
//   fit.same            at the loop's own rate fitLoop returns the same object
//   fit.frames          the sample loop has 371,614 frames at 48000, 743,227 at 96000 and 185,807 at 24000
//   fit.frames.any      every fit of loops of 1, 16, 64 and 2999 frames between eleven rates has
//                       Math.round(framesIn * rate / rateIn) frames, two arrays of that length, the wanted rate and
//                       no NaN; at its own rate the loop itself comes back
//   fit.period.<frames>.<rate>
//                       a loop of sines, each a whole number of cycles in the loop (from 30 Hz to 20 kHz at 44100 Hz,
//                       unequal phases, other sines on the right than on the left), fitted to 48000 and to 96000, is
//                       the same sines at the new length: the error's RMS is at most -100 dB under the signal's, the
//                       worst sample at most -90 dB under the signal's peak. The loops: 341,420 frames (the sample
//                       loop's), 2,999, and two shorter than the kernel's 96 taps, 64 and 16 (the kernel goes round
//                       the loop more than once)
//   fit.seam.<frames>.<rate>
//                       the same two bounds over the first and the last 64 frames alone
//   fit.passband.<hz>   44100 to 48000: a single sine at 20, 100, 1000, 5000, 10000, 15000, 18000 and 20000 Hz keeps
//                       its level within 0.01 dB (0.1 dB at 20000) and its phase within 0.001 radian
//   fit.images.<hz>     44100 to 48000 with a sine at 15000 and one at 20000 Hz: all that is not the sine (the error
//                       against the exact sine) is at most -100 dB under it
//   fit.down.<rate>.<hz>
//                       44100 to 24000: a sine at 5000 Hz comes out within 0.01 dB, sines at 14000 and at 20000 Hz
//                       (above the new half rate) leave at most -90 dB of anything. 44100 to 16000: the same with
//                       3000 Hz (kept) and 10000 Hz (removed)
//   fit.down.<rate>.short
//                       a loop of 64 frames going down (the kernel is 178 or 266 taps: several times round the loop):
//                       its low sines come out with the bounds of fit.period
//   fit.dc.<rate>       a constant loop comes out as the same constant to 1e-6, at 16000, 24000, 48000 and 96000
// The sines are compared at the fitted loop's own length: a sine of c cycles in the loop has c cycles in the fit.
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';

const TEST = 'web.sample';
const FILE_BYTES = 2048600;
const FILE_SHA256 = '0327dec3cbc7de82cf3ed6d9f0533d7035681c20297b0c9cb7aae2bc9a8b7e52';
const FILE_RATE = 44100;
const FILE_FRAMES = 341420;
const TAU = 2 * Math.PI;

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

const [samplePath, wavPath] = process.argv.slice(2);
if (!samplePath || !wavPath) {
  console.error('usage: node sample.mjs <sample.js> <loop.wav>');
  process.exit(2);
}
const { SAMPLE_URL, readWav, fitLoop } = await import(pathToFileURL(samplePath).href);
const exported = SAMPLE_URL === 'audio/loop.wav' && typeof readWav === 'function' && typeof fitLoop === 'function';
row(exported, 'exports', 'SAMPLE_URL is audio/loop.wav, readWav(bytes), fitLoop(loop, sampleRate)');
if (!exported) process.exit(1);

const db = (v) => 20 * Math.log10(v);
const asDb = (v) => `${db(v).toFixed(1)} dB`;

// ---- WAV files in memory --------------------------------------------------------------------------------------------
// A RIFF file of `chunks`, each [id, bytes]. An odd chunk gets its pad byte.
function riff(chunks, form = 'WAVE', magic = 'RIFF') {
  let size = 4;
  for (const [, body] of chunks) size += 8 + body.length + (body.length & 1);
  const out = new Uint8Array(8 + size);
  const view = new DataView(out.buffer);
  const text = (at, s) => {
    for (let i = 0; i < 4; i += 1) out[at + i] = s.charCodeAt(i);
  };
  text(0, magic);
  view.setUint32(4, size, true);
  text(8, form);
  let at = 12;
  for (const [id, body] of chunks) {
    text(at, id);
    view.setUint32(at + 4, body.length, true);
    out.set(body, at + 8);
    at += 8 + body.length + (body.length & 1);
  }
  return out;
}
// The fmt chunk: 16 bytes, or 40 with the extensible tag and `tag` as the sub-format's first two bytes.
function fmt({ tag = 1, channels = 2, rate = 44100, bits = 16, block = channels * bits / 8, extensible = false }) {
  const out = new Uint8Array(extensible ? 40 : 16);
  const view = new DataView(out.buffer);
  view.setUint16(0, extensible ? 0xFFFE : tag, true);
  view.setUint16(2, channels, true);
  view.setUint32(4, rate, true);
  view.setUint32(8, rate * block, true);
  view.setUint16(12, block, true);
  view.setUint16(14, bits, true);
  if (extensible) {
    view.setUint16(16, 22, true);                         // the bytes that follow
    view.setUint16(18, bits, true);                       // the valid bits
    view.setUint32(20, channels === 2 ? 3 : 4, true);     // the speakers
    view.setUint16(24, tag, true);                        // the sub-format: the tag, then what every one ends in
    out.set([0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71], 26);
  }
  return out;
}
// The data chunk's bytes: `samples` (interleaved) as integers of `bits` bits, or as 32-bit floats.
function pcm(samples, bits, float = false) {
  const width = bits / 8;
  const out = new Uint8Array(samples.length * width);
  const view = new DataView(out.buffer);
  samples.forEach((s, i) => {
    const at = i * width;
    if (float) view.setFloat32(at, s, true);
    else if (bits === 8) view.setUint8(at, s);
    else if (bits === 16) view.setInt16(at, s, true);
    else if (bits === 24) {
      view.setUint16(at, s & 0xFFFF, true);
      view.setInt8(at + 2, s >> 16);
    } else view.setInt32(at, s, true);
  });
  return out;
}
const other = (length) => Uint8Array.from({ length }, (_, i) => 0x41 + i);      // a chunk readWav does not know

// readWav(bytes) gives exactly `wantL` and `wantR` (numbers: a Float32Array holds them rounded once).
function reads(name, bytes, rate, wantL, wantR, detail) {
  let got;
  try {
    got = readWav(bytes);
  } catch (e) {
    return row(false, name, `it threw: ${e.message}`);
  }
  const n = wantL.length;
  let same = got.sampleRate === rate && got.frames === n && got.left instanceof Float32Array
      && got.right instanceof Float32Array && got.left.length === n && got.right.length === n
      && got.left !== got.right;
  let at = -1;
  for (let i = 0; i < n && same; i += 1) {
    same = Object.is(got.left[i], Math.fround(wantL[i])) && Object.is(got.right[i], Math.fround(wantR[i]));
    if (!same) at = i;
  }
  return row(same, name, same ? detail
    : at < 0 ? `${got.sampleRate} Hz, ${got.frames} frames, not ${rate} Hz and ${n} in two arrays`
    : `frame ${at} is (${got.left[at]}, ${got.right[at]}), written (${wantL[at]}, ${wantR[at]})`);
}
// readWav(bytes) throws an Error of its own, and the message matches `fault`.
function refuses(name, bytes, fault) {
  try {
    const got = readWav(bytes);
    return row(false, name, `it read ${got.frames} frame(s)`);
  } catch (e) {
    return row(e instanceof Error && e.name === 'Error' && fault.test(e.message), name, `"${e.message}"`);
  }
}

{
  // Stereo frames as integers: the two ends of the range, zero, the smallest steps and some middle values.
  const frames16 = [[-32768, 32767], [0, -1], [1, 12345], [-12345, 256], [-256, 255]];
  const frames24 = [[-8388608, 8388607], [0, -1], [1, 0x123456], [-0x123456, 65536], [-65536, 255], [-256, 32768]];
  const frames32 = [[-2147483648, 2147483647], [0, -1], [1, 0x12345678], [-0x12345678, 1 << 24], [-(1 << 24), 65535]];
  const framesFloat = [[1, -1], [0.5, -0.25], [1.5, -2], [1e-10, 0], [0.1, -0.7], [-0, 3.4e38]].map(
    (f) => f.map(Math.fround));
  const sides = (frames, scale) => [frames.map((f) => f[0] / scale), frames.map((f) => f[1] / scale)];
  const [l16, r16] = sides(frames16, 32768);
  const [l24, r24] = sides(frames24, 8388608);
  const [l32, r32] = sides(frames32, 2147483648);
  const [lF, rF] = sides(framesFloat, 1);
  const data16 = pcm(frames16.flat(), 16);
  const data24 = pcm(frames24.flat(), 24);

  reads('wav.pcm16', riff([['fmt ', fmt({ bits: 16 })], ['data', data16]]), 44100, l16, r16,
        `${l16.length} frames, each sample s / 32768`);
  reads('wav.pcm24', riff([['fmt ', fmt({ bits: 24, rate: 48000 })], ['data', data24]]), 48000, l24, r24,
        `${l24.length} frames at 48000 Hz, each sample s / 8388608`);
  reads('wav.pcm32', riff([['fmt ', fmt({ bits: 32, rate: 96000 })], ['data', pcm(frames32.flat(), 32)]]), 96000,
        l32, r32, `${l32.length} frames at 96000 Hz, each sample s / 2147483648`);
  reads('wav.float32', riff([['fmt ', fmt({ tag: 3, bits: 32 })], ['data', pcm(framesFloat.flat(), 32, true)]]),
        44100, lF, rF, `${lF.length} frames, each float as it was written (1.5 and 3.4e38 too: nothing is clipped)`);
  reads('wav.extensible.pcm', riff([['fmt ', fmt({ bits: 24, extensible: true })], ['data', data24]]), 44100,
        l24, r24, 'tag 0xFFFE over the sub-format 1, 24 bits');
  reads('wav.extensible.float',
        riff([['fmt ', fmt({ tag: 3, bits: 32, extensible: true })], ['data', pcm(framesFloat.flat(), 32, true)]]),
        44100, lF, rF, 'tag 0xFFFE over the sub-format 3, 32-bit float');
  {
    const mono = frames24.flat();
    const want = mono.map((s) => s / 8388608);
    reads('wav.mono', riff([['fmt ', fmt({ bits: 24, channels: 1 })], ['data', pcm(mono, 24)]]), 44100, want, want,
          `${mono.length} frames of one channel: both sides have them, in two arrays`);
    // Three frames of one 24-bit channel are 9 bytes: the data chunk itself is odd, and a chunk follows its pad.
    reads('wav.pad.data', riff([['fmt ', fmt({ bits: 24, channels: 1 })], ['data', pcm(mono.slice(0, 3), 24)],
                                ['LIST', other(6)]]),
          44100, want.slice(0, 3), want.slice(0, 3), 'an odd data chunk of 9 bytes, its pad byte, then a chunk');
  }
  reads('wav.pad', riff([['fmt ', fmt({ bits: 16 })], ['LIST', other(7)], ['data', data16]]), 44100, l16, r16,
        'a chunk of 7 bytes and its pad byte between fmt and data');
  reads('wav.after', riff([['fmt ', fmt({ bits: 16 })], ['data', data16], ['LIST', other(10)]]), 44100, l16, r16,
        'a chunk it does not know after data');
  reads('wav.before', riff([['data', data16], ['cue ', other(5)], ['fmt ', fmt({ bits: 16 })]]), 44100, l16, r16,
        'data, then an odd chunk it does not know, then fmt: any order');
  {
    const file = riff([['JUNK', other(28)], ['fmt ', fmt({ bits: 24 })], ['data', data24]]);
    // The file 5 bytes into a larger buffer, with other bytes around it.
    const held = new Uint8Array(file.length + 16).fill(0xEE);
    held.set(file, 5);
    reads('wav.view.bytes', held.subarray(5, 5 + file.length), 44100, l24, r24,
          'a Uint8Array that starts 5 bytes into its buffer');
    reads('wav.view.data', new DataView(held.buffer, 5, file.length), 44100, l24, r24, 'a DataView of the same');
    reads('wav.buffer', held.buffer.slice(5, 5 + file.length), 44100, l24, r24, 'an ArrayBuffer');
    const more = new Uint8Array(file.length + 9).fill(0xEE);
    more.set(file, 0);
    reads('wav.outside', more, 44100, l24, r24, '9 bytes after the RIFF chunk are not the file: passed over');
  }

  const good = [['fmt ', fmt({ bits: 16 })], ['data', data16]];
  const page = new TextEncoder().encode('<!DOCTYPE html><html><body><h1>404 Not Found</h1></body></html>');
  refuses('wav.refuse.riff', page, /not a RIFF file/);
  refuses('wav.refuse.riff.rifx', riff(good, 'WAVE', 'RIFX'), /not a RIFF file/);
  refuses('wav.refuse.wave', riff(good, 'AVI '), /not WAVE/);
  refuses('wav.refuse.short', riff(good).subarray(0, 11), /not a RIFF\/WAVE file.*11 bytes/);
  refuses('wav.refuse.empty', new ArrayBuffer(0), /not a RIFF\/WAVE file.*0 bytes/);
  refuses('wav.refuse.fmt', riff([['LIST', other(4)], ['data', data16]]), /no fmt chunk/);
  refuses('wav.refuse.data', riff([['fmt ', fmt({ bits: 16 })], ['LIST', other(4)]]), /no data chunk/);
  refuses('wav.refuse.tag', riff([['fmt ', fmt({ tag: 2, bits: 16 })], ['data', data16]]), /format.*tag 2\b/);
  refuses('wav.refuse.tag.extensible', riff([['fmt ', fmt({ tag: 2, bits: 16, extensible: true })], ['data', data16]]),
          /format.*tag 2\b/);
  refuses('wav.refuse.bits', riff([['fmt ', fmt({ bits: 8 })], ['data', pcm([0, 255, 128, 127], 8)]]),
          /format.*\b8 bits/);
  refuses('wav.refuse.float64', riff([['fmt ', fmt({ tag: 3, bits: 64 })], ['data', other(32)]]),
          /format.*tag 3 with 64 bits/);
  refuses('wav.refuse.channels',
          riff([['fmt ', fmt({ bits: 16, channels: 3 })], ['data', pcm([1, 2, 3, 4, 5, 6], 16)]]), /\b3 channels/);
  refuses('wav.refuse.rate', riff([['fmt ', fmt({ bits: 16, rate: 0 })], ['data', data16]]), /sample rate of 0/);
  refuses('wav.refuse.block', riff([['fmt ', fmt({ bits: 24, block: 8 })], ['data', other(16)]]),
          /frame of 8 bytes/);
  refuses('wav.refuse.subformat',
          riff([['fmt ', fmt({ bits: 16, extensible: true }).subarray(0, 18)], ['data', data16]]),
          /extensible.*no sub-format/);
  refuses('wav.refuse.fmt.short', riff([['fmt ', fmt({ bits: 16 }).subarray(0, 14)], ['data', data16]]),
          /fmt chunk has 14 bytes/);
  refuses('wav.refuse.frame', riff([['fmt ', fmt({ bits: 24 })], ['data', new Uint8Array(0)]]),
          /no whole frame.*\b0 bytes/);
  refuses('wav.refuse.frame.part', riff([['fmt ', fmt({ bits: 24 })], ['data', other(5)]]),
          /no whole frame.*\b5 bytes/);
  {
    // Cut as a download is cut: the RIFF size still says the whole file. Then the same cut with a RIFF size that
    // agrees with the bytes, so the chunk itself must be found too long, or its header cut.
    const whole = riff([['fmt ', fmt({ bits: 16 })], ['LIST', other(6)], ['data', data16]]);
    const agreeing = (bytes) => {
      const out = bytes.slice();
      new DataView(out.buffer).setUint32(4, out.length - 8, true);
      return out;
    };
    const inData = whole.subarray(0, whole.length - 7);
    refuses('wav.refuse.cut.data', inData, /cut short.*\b\d+ bytes of \d+/);
    refuses('wav.refuse.long.data', agreeing(inData), /data chunk.*runs past the end/);
    const dataHeader = 12 + 8 + 16 + 8 + 6;                // the data chunk's header starts here
    const inHeader = whole.subarray(0, dataHeader + 5);
    refuses('wav.refuse.cut.header', inHeader, /cut short.*\b\d+ bytes of \d+/);
    refuses('wav.refuse.long.header', agreeing(inHeader), /cut short in a chunk header/);
  }
}

// ---- signals --------------------------------------------------------------------------------------------------------
// One side of a loop of sines: `parts` is a list of [cycles in the loop, phase], each sine 1 / (their number) high.
function sineSum(parts, k, frames) {
  let s = 0;
  for (const [cycles, phase] of parts) s += Math.sin(TAU * ((cycles * k) % frames) / frames + phase);
  return s / parts.length;
}
function sineLoop(frames, partsL, partsR, sampleRate = FILE_RATE) {
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  for (let i = 0; i < frames; i += 1) {
    left[i] = sineSum(partsL, i, frames);
    right[i] = sineSum(partsR, i, frames);
  }
  return { sampleRate, frames, left, right };
}
// The sines of these frequencies (at 44100 Hz) that fit a loop of `frames`: the nearest whole numbers of cycles, each
// once, with phases that differ. `turn` sets the two sides apart.
function partsFor(hzList, frames, turn) {
  const cycles = [...new Set(hzList.map((hz) => Math.max(1, Math.round(hz * frames / FILE_RATE))))];
  return cycles.map((c, q) => [c, turn + (0.7 + 0.4 * turn) * q]);
}
// One side of a fitted loop against its sines at the fit's own length: the squares of the error and of the signal,
// the worst error and the signal's peak, over the whole side and over its first and last 64 frames.
function sideError(x, parts) {
  const m = x.length;
  const e = { squares: 0, signal: 0, worst: 0, peak: 0, seamSquares: 0, seamWorst: 0, seamCount: 0 };
  for (let k = 0; k < m; k += 1) {
    const want = sineSum(parts, k, m);
    const d = x[k] - want;
    e.squares += d * d;
    e.signal += want * want;
    e.worst = Math.max(e.worst, Math.abs(d));
    e.peak = Math.max(e.peak, Math.abs(want));
    if (k < 64 || k >= m - 64) {
      e.seamSquares += d * d;
      e.seamWorst = Math.max(e.seamWorst, Math.abs(d));
      e.seamCount += 1;
    }
  }
  return e;
}
// Both sides together, as ratios to the signal: its RMS for the two RMS values, its peak for the two worst samples.
function loopError(fitted, partsL, partsR) {
  const a = sideError(fitted.left, partsL);
  const b = sideError(fitted.right, partsR);
  const signal = Math.sqrt((a.signal + b.signal) / (2 * fitted.frames));
  const peak = Math.max(a.peak, b.peak);
  return {
    rms: Math.sqrt((a.squares + b.squares) / (2 * fitted.frames)) / signal,
    worst: Math.max(a.worst, b.worst) / peak,
    seamRms: Math.sqrt((a.seamSquares + b.seamSquares) / (a.seamCount + b.seamCount)) / signal,
    seamWorst: Math.max(a.seamWorst, b.seamWorst) / peak,
  };
}
// One side of a fitted loop as one sine of `cycles` in the loop: its level (the input's is 1) and its phase.
function sineOf(x, cycles) {
  const m = x.length;
  let a = 0;
  let b = 0;
  for (let k = 0; k < m; k += 1) {
    const angle = TAU * ((cycles * k) % m) / m;
    a += x[k] * Math.sin(angle);
    b += x[k] * Math.cos(angle);
  }
  return { level: 2 * Math.hypot(a, b) / m, phase: Math.atan2(b, a) };
}
function facts(x, from = 0, to = x.length) {
  let peak = 0;
  let at = from;
  let squares = 0;
  let sum = 0;
  for (let i = from; i < to; i += 1) {
    if (Math.abs(x[i]) > peak) {
      peak = Math.abs(x[i]);
      at = i;
    }
    squares += x[i] * x[i];
    sum += x[i];
  }
  return { peak, at, rms: Math.sqrt(squares / (to - from)), mean: sum / (to - from) };
}
const RMS_BOUND = 1e-5;                 // -100 dB
const WORST_BOUND = 10 ** -4.5;         // -90 dB

// ---- the file, and the file fitted ----------------------------------------------------------------------------------
let file = null;
try {
  const bytes = readFileSync(wavPath);
  row(bytes.length === FILE_BYTES, 'file.size', `${bytes.length} bytes`);
  const sha = createHash('sha256').update(bytes).digest('hex');
  row(sha === FILE_SHA256, 'file.sha256', sha);
  file = readWav(bytes);
} catch (e) {
  row(false, 'file.read', e.message);
}
if (file) {
  const { left: L, right: R, frames: n } = file;
  row(file.sampleRate === FILE_RATE && n === FILE_FRAMES && L.length === n && R.length === n, 'file.format',
      `${file.sampleRate} Hz, ${n} frames (${(n / file.sampleRate).toFixed(3)} s)`);
  const l = facts(L);
  const r = facts(R);
  row(l.peak < 1 && r.peak < 1, 'file.peak',
      `${db(l.peak).toFixed(2)} and ${db(r.peak).toFixed(2)} dBFS, at frames ${l.at} and ${r.at}: no sample at +-1`);
  row([l, r].every((s) => db(s.rms) >= -17.5 && db(s.rms) <= -15), 'file.rms',
      `${db(l.rms).toFixed(2)} and ${db(r.rms).toFixed(2)} dBFS (the window is -17.5 to -15)`);
  row(Math.abs(l.mean) < 0.002 && Math.abs(r.mean) < 0.002, 'file.mean',
      `${l.mean.toExponential(1)} and ${r.mean.toExponential(1)}`);
  const ms = Math.round(0.001 * file.sampleRate);
  const endL = facts(L, n - ms).rms;
  const endR = facts(R, n - ms).rms;
  row(db(endL) < -60 && db(endR) < -60, 'file.end',
      `the last ${ms} frames: ${db(endL).toFixed(1)} and ${db(endR).toFixed(1)} dBFS RMS`);
  row(Math.abs(L[0]) < 0.02 && Math.abs(R[0]) < 0.02, 'file.start',
      `frame 0 is (${L[0].toFixed(6)}, ${R[0].toFixed(6)})`);

  // The first fit of this run, as the page makes it: nothing has run fitLoop before.
  let t0 = performance.now();
  const at48 = fitLoop(file, 48000);
  const first = performance.now() - t0;
  t0 = performance.now();
  const again = fitLoop(file, 48000);
  const second = performance.now() - t0;
  const at96 = fitLoop(file, 96000);
  const at24 = fitLoop(file, 24000);

  row(fitLoop(file, file.sampleRate) === file, 'fit.same', 'at 44100 the loop itself comes back');
  row(at48.frames === 371614 && at96.frames === 743227 && at24.frames === 185807
      && [at48, at96, at24].every((f) => f.left.length === f.frames && f.right.length === f.frames)
      && at48.sampleRate === 48000 && at96.sampleRate === 96000 && at24.sampleRate === 24000, 'fit.frames',
      `${at48.frames} at 48000, ${at96.frames} at 96000, ${at24.frames} at 24000, ${n} at 44100`);

  const fl = facts(at48.left);
  const fr = facts(at48.right);
  const offL = db(fl.rms) - db(l.rms);
  const offR = db(fr.rms) - db(r.rms);
  row(Math.abs(offL) <= 0.01 && Math.abs(offR) <= 0.01, 'fit.sample.rms',
      `${db(fl.rms).toFixed(3)} and ${db(fr.rms).toFixed(3)} dBFS at 48000: ${offL.toExponential(1)} and `
      + `${offR.toExponential(1)} dB from the file's (the bound is 0.01)`);
  const m = at48.frames;
  const end48L = facts(at48.left, m - 48).rms;
  const end48R = facts(at48.right, m - 48).rms;
  row(db(end48L) < -60 && db(end48R) < -60, 'fit.sample.end',
      `the last 48 frames: ${db(end48L).toFixed(1)} and ${db(end48R).toFixed(1)} dBFS RMS`);
  row(Math.abs(at48.left[0]) < 0.05 && Math.abs(at48.right[0]) < 0.05, 'fit.sample.start',
      `frame 0 is (${at48.left[0].toFixed(6)}, ${at48.right[0].toFixed(6)})`);
  let same = again.frames === m;
  for (let i = 0; i < m && same; i += 1) {
    same = Object.is(at48.left[i], again.left[i]) && Object.is(at48.right[i], again.right[i]);
  }
  row(same, 'fit.sample.deterministic', 'a second fit gives the same samples');
  row(first < 5000, 'fit.sample.time', `${first.toFixed(0)} ms (the bound is 5000: a time is no gate)`);
  note(`fitLoop took ${first.toFixed(0)} ms for the sample loop at 48000 (the first call of this run, as the page `
       + `makes it) and ${second.toFixed(0)} ms the second time`);
  const peak = (f) => db(Math.max(facts(f.left).peak, facts(f.right).peak)).toFixed(3);
  note(`the largest sample is ${peak(file)} dBFS in the file, ${peak(at48)} dBFS at 48000 and ${peak(at96)} dBFS at `
       + '96000 (a fit moves the peaks, and nothing here clips them)');
}

// ---- the frames of any fit ------------------------------------------------------------------------------------------
{
  const rates = [8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 192000];
  let fits = 0;
  let wrong = '';
  for (const frames of [1, 16, 64, 2999]) {
    for (const from of [8000, 44100, 48000]) {
      const loop = sineLoop(frames, [[1, 0.4]], [[1, 1.9]], from);
      for (const to of rates) {
        const fitted = fitLoop(loop, to);
        const want = Math.round(frames * to / from);
        let ok = fitted.frames === want && fitted.sampleRate === to && fitted.left instanceof Float32Array
            && fitted.right instanceof Float32Array && fitted.left.length === want && fitted.right.length === want
            && (to !== from || fitted === loop);
        for (let i = 0; i < want && ok; i += 1) {
          ok = Number.isFinite(fitted.left[i]) && Number.isFinite(fitted.right[i]);
        }
        if (!ok && !wrong) wrong = `${frames} frames from ${from} to ${to}: ${fitted.frames} frames, wanted ${want}`;
        fits += 1;
      }
    }
  }
  row(wrong === '', 'fit.frames.any',
      wrong || `${fits} fits of 1, 16, 64 and 2999 frames: Math.round(framesIn * rate / rateIn) frames, no NaN`);
}

// ---- a sum of sines is the same sum at the new rate -----------------------------------------------------------------
{
  const hzL = [30, 100, 440, 1000, 5000, 10000, 15000, 20000];
  const hzR = [50, 250, 2000, 7000, 12000, 16000, 18000, 20000];
  for (const frames of [FILE_FRAMES, 2999, 64, 16]) {
    const partsL = partsFor(hzL, frames, 0.3);
    const partsR = partsFor(hzR, frames, 2.0);
    const loop = sineLoop(frames, partsL, partsR);
    const top = Math.max(...partsL.map((p) => p[0])) * FILE_RATE / frames;
    for (const rate of [48000, 96000]) {
      const fitted = fitLoop(loop, rate);
      const e = loopError(fitted, partsL, partsR);
      row(e.rms <= RMS_BOUND && e.worst <= WORST_BOUND, `fit.period.${frames}.${rate}`,
          `${partsL.length} and ${partsR.length} sines up to ${top.toFixed(0)} Hz, ${fitted.frames} frames: the error `
          + `is ${asDb(e.rms)} RMS (the bound is -100), its worst sample ${asDb(e.worst)} (-90)`);
      row(e.seamRms <= RMS_BOUND && e.seamWorst <= WORST_BOUND, `fit.seam.${frames}.${rate}`,
          `the first and the last 64 frames: ${asDb(e.seamRms)} RMS (-100), the worst sample ${asDb(e.seamWorst)} `
          + '(-90)');
    }
  }
}

// ---- single sines: the pass band, the images, going down ------------------------------------------------------------
{
  // One second and a frame: f cycles in the loop are f Hz (within half a Hz), and no two output frames share a phase.
  const frames = 44101;
  const phaseL = 0.5;
  const phaseR = 2.5;
  const single = (hzL, hzR) => sineLoop(frames, [[hzL, phaseL]], [[hzR, phaseR]]);
  const turn = (a) => Math.abs(Math.atan2(Math.sin(a), Math.cos(a)));

  for (const [hzL, hzR] of [[20, 100], [1000, 5000], [10000, 15000], [18000, 20000]]) {
    const fitted = fitLoop(single(hzL, hzR), 48000);
    for (const [hz, x, phase] of [[hzL, fitted.left, phaseL], [hzR, fitted.right, phaseR]]) {
      const s = sineOf(x, hz);
      const bound = hz === 20000 ? 0.1 : 0.01;
      row(Math.abs(db(s.level)) <= bound && turn(s.phase - phase) <= 0.001, `fit.passband.${hz}`,
          `the level moved by ${db(s.level).toExponential(1)} dB (the bound is ${bound}), the phase by `
          + `${turn(s.phase - phase).toExponential(1)} radian (0.001)`);
    }
  }

  {
    // At the sample loop's own length: the ratio the page has.
    const cycles = (hz) => Math.round(hz * FILE_FRAMES / FILE_RATE);
    const parts = [[[cycles(15000), phaseL]], [[cycles(20000), phaseR]]];
    const fitted = fitLoop(sineLoop(FILE_FRAMES, parts[0], parts[1]), 48000);
    [[15000, fitted.left], [20000, fitted.right]].forEach(([hz, x], side) => {
      const e = sideError(x, parts[side]);
      const rms = Math.sqrt(e.squares / e.signal);
      row(rms <= RMS_BOUND, `fit.images.${hz}`,
          `all that is not the sine is ${asDb(rms)} under it (the bound is -100), its worst sample `
          + `${asDb(e.worst / e.peak)}`);
    });
  }

  for (const [rate, kept, removed] of [[24000, [5000], [14000, 20000]], [16000, [3000], [10000]]]) {
    for (const hz of kept) {
      const fitted = fitLoop(single(hz, hz), rate);
      const s = sineOf(fitted.left, hz);
      const e = sideError(fitted.right, [[hz, phaseR]]);
      row(Math.abs(db(s.level)) <= 0.01 && turn(s.phase - phaseL) <= 0.001, `fit.down.${rate}.${hz}`,
          `kept: the level moved by ${db(s.level).toExponential(1)} dB (the bound is 0.01), the phase by `
          + `${turn(s.phase - phaseL).toExponential(1)} radian; the error is ${asDb(Math.sqrt(e.squares / e.signal))}`);
    }
    for (const hz of removed) {
      const fitted = fitLoop(single(hz, hz), rate);
      // Against the input sine's RMS, 1 / sqrt(2).
      const left = Math.max(facts(fitted.left).rms, facts(fitted.right).rms) * Math.SQRT2;
      row(left <= WORST_BOUND, `fit.down.${rate}.${hz}`,
          `removed (the new half rate is ${rate / 2} Hz): ${asDb(left)} of it is left (the bound is -90)`);
    }
    // 64 frames: the kernel goes round the loop several times. 3 and 5 cycles are 2067 and 3445 Hz.
    const partsL = [[3, phaseL]];
    const partsR = [[5, phaseR], [1, 0.9]];
    const fitted = fitLoop(sineLoop(64, partsL, partsR), rate);
    const e = loopError(fitted, partsL, partsR);
    row(e.rms <= RMS_BOUND && e.worst <= WORST_BOUND, `fit.down.${rate}.short`,
        `64 frames to ${fitted.frames}: the error is ${asDb(e.rms)} RMS (the bound is -100), its worst sample `
        + `${asDb(e.worst)} (-90)`);
  }
}

// ---- a constant stays that constant ---------------------------------------------------------------------------------
for (const rate of [16000, 24000, 48000, 96000]) {
  let worst = 0;
  for (const frames of [64, 2999]) {
    const loop = { sampleRate: FILE_RATE, frames, left: new Float32Array(frames).fill(0.25),
                   right: new Float32Array(frames).fill(-0.6) };
    const fitted = fitLoop(loop, rate);
    if (fitted.frames === 0) worst = Infinity;
    for (let i = 0; i < fitted.frames; i += 1) {
      worst = Math.max(worst, Math.abs(fitted.left[i] - 0.25), Math.abs(fitted.right[i] - Math.fround(-0.6)));
    }
  }
  row(worst <= 1e-6, `fit.dc.${rate}`,
      `0.25 and -0.6 over 64 and over 2999 frames: off by at most ${worst.toExponential(1)} (the bound is 1e-6)`);
}
note('the samples are this JavaScript engine\'s: a browser\'s differ in the last bits, so the only hash is the '
     + 'file\'s');

console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
