// web/loop.js: the demo's built-in loop, made by the page (ADR-93, web Sprint D). No audio file is in the repository.
//
//   import { synthLoop } from './loop.js';
//   const { sampleRate, frames, left, right } = synthLoop(context.sampleRate);
//
// Four bars of 4/4 at 100 BPM, stereo: 16 beats of round(0.6 s * sampleRate) frames each, so the loop is a whole number
// of beats and every beat a whole number of frames (9.6 s at 44.1, 48 and 96 kHz).
//   kick     a sine swept from 150 to 50 Hz with a 2 ms click: beats 1 and 3, and a pickup in bars 2 and 4
//   snare    a 190 Hz body under band-passed noise: beats 2 and 4
//   hat      high-passed noise on the eighths, alternating in level, a little to the right
//   bass     one sustained note a bar, three harmonics through a low-pass: the level a compressor rides
//   pad      three detuned sines a side, swelling over two bars: the stereo sustain
// The drums keep their transients (nothing here limits or clips), the bass and the pad never stop, and the mix is
// scaled so its largest sample is -3 dBFS; the RMS then lies between -16 and -13 dBFS (web/tests/loop.mjs holds both).
//
// Deterministic: no Math.random, so one browser makes the same samples every time. Math.sin and Math.exp differ in
// the last bit between JavaScript engines, so two browsers' loops are equal to the ear, not to the bit: a test checks
// the loop's properties, never a hash.
//
// No click at the seam: a voice that passes the end is added on at the start, every bass note is a whole number of
// cycles and fades at both ends, and every pad partial is a whole number of cycles over the loop. No DC: each channel's
// mean is taken out before the level is set.

const BPM = 100;
const BEATS = 16;
const PEAK_DBFS = -3;
// The sustained voices against a kick of 0.80: with these the RMS is -14.7 dBFS under the -3 dBFS peaks, and a 50 ms
// window ranges from -19.5 to -9.7 dBFS.
const BASS_GAIN = 0.62;
const PAD_GAIN = 0.05;
const TAU = 2 * Math.PI;

export function synthLoop(sampleRate) {
  const beat = Math.round(sampleRate * 60 / BPM);              // 28800 frames at 48 kHz
  const n = BEATS * beat;
  const seconds = n / sampleRate;
  const left = new Float32Array(n);
  const right = new Float32Array(n);
  const mixL = new Float64Array(n);
  const mixR = new Float64Array(n);

  // One-pole coefficients from a corner frequency, so the voices sound alike at every rate.
  const lowpass = (hz) => 1 - Math.exp(-TAU * hz / sampleRate);
  const highpass = (hz) => 1 / (1 + TAU * hz / sampleRate);

  // xorshift32, in [-1, 1): the same noise everywhere.
  let seed = 0x2f6e2b1;
  const noise = () => {
    seed ^= seed << 13; seed >>>= 0;
    seed ^= seed >>> 17;
    seed ^= seed << 5; seed >>>= 0;
    return seed / 2147483648 - 1;
  };
  // A voice of `length` frames from frame `at`, wrapped around the end.
  const add = (at, length, gainL, gainR, voice) => {
    const start = Math.round(at);
    for (let i = 0; i < length; i += 1) {
      const s = voice(i / sampleRate, i);
      const k = (start + i) % n;
      mixL[k] += gainL * s;
      mixR[k] += gainR * s;
    }
  };

  // ---- kick ---------------------------------------------------------------------------------------------------------
  const kick = (t) => {
    const phase = TAU * (50 * t + 100 * 0.035 * (1 - Math.exp(-t / 0.035)));      // 150 Hz falling to 50 Hz
    const body = Math.exp(-t / 0.16) * Math.min(1, t / 0.0005) * Math.sin(phase);
    const click = t < 0.002 ? 0.5 * (1 - t / 0.002) * Math.sin(TAU * 1800 * t) : 0;
    return body + click;
  };
  const kickFrames = Math.round(0.6 * sampleRate);
  for (let bar = 0; bar < 4; bar += 1) {
    add((bar * 4 + 0) * beat, kickFrames, 0.80, 0.80, kick);
    add((bar * 4 + 2) * beat, kickFrames, 0.80, 0.80, kick);
    if (bar % 2 === 1) add((bar * 4 + 2.75) * beat, kickFrames, 0.55, 0.55, kick);
  }

  // ---- snare --------------------------------------------------------------------------------------------------------
  const snareFrames = Math.round(0.3 * sampleRate);
  const snareLow = lowpass(6100);
  const snareHigh = highpass(850);
  for (let bar = 0; bar < 4; bar += 1) {
    for (let hit = 1; hit < 4; hit += 2) {
      let low = 0;
      let high = 0;
      let before = 0;
      add((bar * 4 + hit) * beat, snareFrames, 0.50, 0.50, (t) => {
        low += snareLow * (noise() - low);
        high = snareHigh * (high + low - before);
        before = low;
        const rattle = 0.9 * high * Math.exp(-t / 0.07);
        const body = 0.7 * Math.sin(TAU * 190 * t) * Math.exp(-t / 0.045);
        return Math.min(1, t / 0.0003) * (rattle + body);
      });
    }
  }

  // ---- hats ---------------------------------------------------------------------------------------------------------
  const hatFrames = Math.round(0.08 * sampleRate);
  const hatHigh = highpass(5100);
  for (let eighth = 0; eighth < BEATS * 2; eighth += 1) {
    let high = 0;
    let before = 0;
    const level = eighth % 2 === 0 ? 0.16 : 0.09;
    add(eighth * beat / 2, hatFrames, level * 0.8, level * 1.2, (t) => {
      const w = noise();
      high = hatHigh * (high + w - before);
      before = w;
      return high * Math.exp(-t / 0.018) * Math.min(1, t / 0.0002);
    });
  }

  // ---- bass: A1, E1, F1, G1, each moved to the nearest whole number of cycles in its bar ----------------------------
  const barFrames = 4 * beat;
  const barSeconds = barFrames / sampleRate;
  const bassNotes = [55.0, 41.2, 43.65, 49.0];
  const bassLow = lowpass(2200);
  for (let bar = 0; bar < 4; bar += 1) {
    const f = Math.round(bassNotes[bar] * barSeconds) / barSeconds;
    let low = 0;
    add(bar * barFrames, barFrames, BASS_GAIN, BASS_GAIN, (t, i) => {
      const s = Math.sin(TAU * f * t) + 0.5 * Math.sin(TAU * 2 * f * t) + 0.25 * Math.sin(TAU * 3 * f * t);
      const fadeIn = Math.min(1, i / (0.008 * sampleRate));
      const fadeOut = Math.min(1, (barFrames - i) / (0.03 * sampleRate));
      low += bassLow * (s - low);
      return low * fadeIn * fadeOut * (0.75 + 0.25 * Math.exp(-t / 0.4));
    });
  }

  // ---- pad: whole cycles over the loop, so it runs through the seam -------------------------------------------------
  const whole = (hz) => Math.round(hz * seconds) / seconds;
  const padL = [whole(220.0), whole(329.6), whole(440.3)];
  const padR = [whole(220.4), whole(330.1), whole(439.6)];
  for (let i = 0; i < n; i += 1) {
    const t = i / sampleRate;
    const swell = 0.6 + 0.4 * Math.sin(TAU * t / (seconds / 2) - Math.PI / 2);
    let l = 0;
    let r = 0;
    for (let k = 0; k < 3; k += 1) {
      l += Math.sin(TAU * padL[k] * t);
      r += Math.sin(TAU * padR[k] * t);
    }
    mixL[i] += PAD_GAIN * swell * l;
    mixR[i] += PAD_GAIN * swell * r;
  }

  // ---- no DC, then the level: the larger channel's peak at -3 dBFS --------------------------------------------------
  let meanL = 0;
  let meanR = 0;
  for (let i = 0; i < n; i += 1) {
    meanL += mixL[i];
    meanR += mixR[i];
  }
  meanL /= n;
  meanR /= n;
  let peak = 0;
  for (let i = 0; i < n; i += 1) {
    mixL[i] -= meanL;
    mixR[i] -= meanR;
    peak = Math.max(peak, Math.abs(mixL[i]), Math.abs(mixR[i]));
  }
  const gain = Math.pow(10, PEAK_DBFS / 20) / peak;
  for (let i = 0; i < n; i += 1) {
    left[i] = gain * mixL[i];
    right[i] = gain * mixR[i];
  }
  return { sampleRate, frames: n, left, right };
}
