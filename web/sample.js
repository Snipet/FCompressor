// web/sample.js: the demo's sample loop, read from its file and fitted to the audio context's rate (ADR-93, the sample
// loop: docs/sprints/web-loop.md). The file is web/audio/loop.wav, made for the demo: 16 beats at 124 BPM, stereo,
// 44.1 kHz, 24 bits. Nothing here touches the page, the network or a clock: the page fetches the bytes, and node runs
// this file as it is.
//
//   import { SAMPLE_URL, readWav, fitLoop } from './sample.js';
//   const file = readWav(await (await fetch(SAMPLE_URL)).arrayBuffer());     // { sampleRate, frames, left, right }
//   const loop = fitLoop(file, context.sampleRate);                          // one period at the context's rate
//
// readWav(bytes) reads a RIFF/WAVE file from an ArrayBuffer or a typed array: PCM of 16, 24 and 32 bits and 32-bit
// float (format tags 1, 3 and 0xFFFE), one channel (both sides get it) or two. The chunks may come in any order, an
// odd chunk has its pad byte, and a chunk it does not know is passed over, as are bytes after the RIFF chunk. An
// integer sample s of b bits is s / 2^(b-1). Anything else it refuses with an Error that says what is wrong. A RIFF
// chunk that runs past the end of the bytes is refused too, and a chunk that runs past the end of the RIFF chunk: a
// download cut short is never played short. So is a RIFF chunk of under 4 bytes: it holds the word WAVE at least.
//
// fitLoop(loop, sampleRate) gives the loop as ONE PERIOD at another rate, in the same shape, with
// frames = Math.round(loop.frames * sampleRate / loop.sampleRate). The input is taken as periodic (its index is
// modulo its frames), and output frame k lies at k * (frames in) / (frames out) input frames, exactly. So the result
// is itself one exact period: it loops with no seam and needs no fade. The ratio is that of the frame counts, not of
// the rates. The two differ by under half a frame in the loop (1.1 parts in a million for the sample loop at 48 kHz):
// no pitch that anyone hears. When the rates are equal it returns `loop` itself.
//
// Why the page does this itself: START asks for a 48 kHz context, so the 44.1 kHz file is resampled for nearly every
// visitor. decodeAudioData resamples a file as a one-shot, with silence before and after: that is a seam every 7.7 s
// when it loops, and each browser has its own filter. A buffer source at another rate than the context's is
// interpolated linearly in one engine. This resampling is periodic, it is the same in every browser, and node tests
// it (web/tests/sample.mjs).
//
// The filter is a Kaiser-windowed sinc: linear phase, no delay.
//   taps      96 when the rate goes up: 48 input frames to each side of an output frame. When the rate goes down the
//             kernel widens by rate in / rate out (178 taps from 44.1 to 24 kHz), so the cut-off follows the new rate.
//   window    Kaiser, beta 12.5
//   cut-off   0.495 of the LOWER rate (21,830 Hz for the file at 48 kHz). The kernel is flat up to 0.4535 of that
//             rate (20 kHz of 44.1) and leaves nothing from 0.5465 of it on (24.1 kHz, where the image of 20 kHz
//             lies), both to -120 dB. Between the two it falls: what the file holds above 20 kHz comes out in part,
//             with a part of its image. Going down to 24 kHz, a part of what lies between 12 and 12.9 kHz folds
//             back under 12 kHz.
//   table     the kernel at 1024 points in each period of the lower rate (100,354 doubles), read with linear
//             interpolation. The table adds an error near -123 dB at 20 kHz, and less below.
// Every output frame has its own set of weights (44.1 to 48 kHz over this loop is 185,807 different phases). The set
// is used for both channels, summed in double precision, and scaled to sum to 1: a constant stays that constant.
// Measured from 44.1 to 48 kHz: a sum of sines from 30 Hz to 20 kHz comes out with an error of -124 dB, a lone sine
// at 20 kHz with -116 dB (the worst case: the edge of the flat band, and its image the nearest; the kernel's -120 dB
// and the table's -123 dB together).
//
// What it costs: once, at START, about 60 ms for the sample loop at 48 kHz under node on an Apple M5 (36 million
// weights, each used twice). While it runs it holds the table (0.8 MB) and one more copy of the input.
//
// Deterministic: no Math.random and no time. Math.sin differs in the last bit between JavaScript engines, so two
// browsers' loops are equal to the ear, not to the bit: the test holds the loop's properties, and a hash only of the
// file.

export const SAMPLE_URL = 'audio/loop.wav';

const HALF = 48;          // the kernel reaches this many periods of the lower rate to each side
const BETA = 12.5;        // the Kaiser window's shape
const CUTOFF = 0.495;     // as a part of the lower rate
const STEPS = 1024;       // table entries in one period of the lower rate

// ---- the file -------------------------------------------------------------------------------------------------------
export function readWav(bytes) {
  const view = ArrayBuffer.isView(bytes) ? new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
                                         : new DataView(bytes);
  const size = view.byteLength;
  const name = (at) => String.fromCharCode(view.getUint8(at), view.getUint8(at + 1), view.getUint8(at + 2),
                                           view.getUint8(at + 3));
  if (size < 12) throw new Error(`not a RIFF/WAVE file: it has only ${size} bytes`);
  if (name(0) !== 'RIFF') throw new Error('not a RIFF file');
  if (name(8) !== 'WAVE') throw new Error('a RIFF file, but not WAVE');
  // The RIFF chunk holds the word WAVE and every other chunk. What follows it is not the file's.
  const riff = view.getUint32(4, true);
  if (riff < 4) throw new Error(`the RIFF chunk says ${riff} bytes: 4 is the least, for the word WAVE`);
  const end = 8 + riff;
  if (end > size) throw new Error(`the file is cut short: it has ${size} bytes of ${end}`);
  const early = end < size;                           // the RIFF chunk ends before the bytes do

  let format = null;
  let data = null;
  for (let at = 12; at < end;) {
    if (at + 8 > end) {
      throw new Error(`${early ? 'the RIFF chunk ends' : 'the file is cut short'} in a chunk header, at byte ${at}`);
    }
    const id = name(at);
    const length = view.getUint32(at + 4, true);
    if (at + 8 + length > end) {
      throw new Error(`the ${id.trim()} chunk at byte ${at} runs past the end of the ${early ? 'RIFF chunk' : 'file'}`
                      + `: it says ${length} bytes, ${end - at - 8} are there`);
    }
    if (id === 'fmt ' && !format) format = { at: at + 8, length };
    if (id === 'data' && !data) data = { at: at + 8, length };
    at += 8 + length + (length & 1);                // an odd chunk has a pad byte after it
  }
  if (!format) throw new Error('the file has no fmt chunk');
  if (!data) throw new Error('the file has no data chunk');

  if (format.length < 16) throw new Error(`the fmt chunk has ${format.length} bytes: 16 is the least`);
  let tag = view.getUint16(format.at, true);
  const channels = view.getUint16(format.at + 2, true);
  const sampleRate = view.getUint32(format.at + 4, true);
  const block = view.getUint16(format.at + 12, true);
  const bits = view.getUint16(format.at + 14, true);
  if (tag === 0xFFFE) {
    // The extensible tag: the real one is the first two bytes of the sub-format, 24 bytes into the chunk.
    if (format.length < 40) throw new Error('the fmt chunk has the extensible tag and no sub-format');
    tag = view.getUint16(format.at + 24, true);
  }
  const float = tag === 3 && bits === 32;
  if (!float && !(tag === 1 && (bits === 16 || bits === 24 || bits === 32))) {
    throw new Error(`a format this does not read: tag ${tag} with ${bits} bits (it reads PCM of 16, 24 and 32 bits `
                    + 'and 32-bit float)');
  }
  if (channels !== 1 && channels !== 2) throw new Error(`${channels} channels: this reads one or two`);
  if (sampleRate === 0) throw new Error('the fmt chunk gives a sample rate of 0');
  const width = bits / 8;
  if (block !== channels * width) {
    throw new Error(`a frame of ${block} bytes: ${channels} channel(s) of ${bits} bits are ${channels * width}`);
  }
  const frames = Math.floor(data.length / block);
  if (frames === 0) throw new Error(`no whole frame: the data chunk has ${data.length} bytes, a frame has ${block}`);

  const sample = float ? (at) => view.getFloat32(at, true)
    : bits === 16 ? (at) => view.getInt16(at, true) / 32768
    : bits === 24 ? (at) => (view.getUint16(at, true) | view.getInt8(at + 2) << 16) / 8388608
    : (at) => view.getInt32(at, true) / 2147483648;
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  for (let i = 0, at = data.at; i < frames; i += 1, at += block) {
    left[i] = sample(at);
    right[i] = channels === 2 ? sample(at + width) : left[i];
  }
  return { sampleRate, frames, left, right };
}

// ---- the kernel -----------------------------------------------------------------------------------------------------
// The modified Bessel function of the first kind and order 0, by its power series: the Kaiser window is made of it.
function besselI0(x) {
  const q = x * x / 4;
  let term = 1;
  let sum = 1;
  for (let k = 1; term > 1e-17 * sum; k += 1) {
    term *= q / (k * k);
    sum += term;
  }
  return sum;
}

// The kernel from -(HALF + 1) to HALF + 1 periods of the lower rate, STEPS entries a period, zero beyond HALF. Its
// peak is 1 and not its true gain: fitLoop scales every frame's weights.
function kernelTable() {
  const centre = (HALF + 1) * STEPS;
  const table = new Float64Array(2 * centre + 2);       // linear interpolation reads one entry further
  const top = besselI0(BETA);
  table[centre] = 1;
  for (let i = 1; i <= HALF * STEPS; i += 1) {
    const t = i / STEPS;
    const x = Math.PI * 2 * CUTOFF * t;
    const u = t / HALF;
    const h = Math.sin(x) / x * besselI0(BETA * Math.sqrt(1 - u * u)) / top;
    table[centre + i] = h;
    table[centre - i] = h;
  }
  return table;
}

// ---- the fit --------------------------------------------------------------------------------------------------------
export function fitLoop(loop, sampleRate) {
  if (sampleRate === loop.sampleRate) return loop;
  const framesIn = loop.frames;
  const frames = Math.round(framesIn * sampleRate / loop.sampleRate);
  const left = new Float32Array(frames);
  const right = new Float32Array(frames);
  const fitted = { sampleRate, frames, left, right };
  if (frames === 0) return fitted;                      // too short for one frame at this rate

  // The cut-off is a part of the lower rate: going down, the kernel is stretched over more input frames.
  const scale = Math.min(1, frames / framesIn);
  const reach = Math.ceil(HALF / scale);                // input frames to each side of an output frame
  const taps = 2 * reach;
  const step = scale * STEPS;                           // table entries from one tap to the next
  const centre = (HALF + 1) * STEPS;
  const table = kernelTable();

  // The input with `reach` of its own frames before it and after it, so that no tap needs a modulo. A loop shorter
  // than the kernel goes round more than once.
  const wrapped = (channel) => {
    const out = new Float32Array(framesIn + taps);
    let from = (framesIn - reach % framesIn) % framesIn;
    for (let i = 0; i < out.length; i += 1) {
      out[i] = channel[from];
      from = from + 1 === framesIn ? 0 : from + 1;
    }
    return out;
  };
  const inL = wrapped(loop.left);
  const inR = wrapped(loop.right);

  // Output frame k lies at k * framesIn / frames input frames: `whole` frames and `rest / frames` of one. Both are
  // kept as integers, so the place is exact however long the loop is.
  const stepWhole = Math.floor(framesIn / frames);
  const stepRest = framesIn % frames;
  let whole = 0;
  let rest = 0;
  for (let k = 0; k < frames; k += 1) {
    // Tap i is input frame whole - reach + 1 + i (it is inL[whole + 1 + i]), and its weight is the table at
    // first + i * step.
    const first = centre + step * (1 - reach - rest / frames);
    let sum = 0;
    let l = 0;
    let r = 0;
    for (let i = 0, at = whole + 1; i < taps; i += 1, at += 1) {
      const x = first + i * step;
      const e = Math.floor(x);
      const w = table[e] + (x - e) * (table[e + 1] - table[e]);
      sum += w;
      l += w * inL[at];
      r += w * inR[at];
    }
    left[k] = l / sum;                                  // the weights, scaled to sum to 1
    right[k] = r / sum;
    whole += stepWhole;
    rest += stepRest;
    if (rest >= frames) {
      rest -= frames;
      whole += 1;
    }
  }
  return fitted;
}
