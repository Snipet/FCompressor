// loop.js (SCOUT SCRATCH): the demo's built-in loop, synthesised by the page. Deterministic: no Math.random, the
// same samples for the same sample rate. Four bars of 4/4 at 100 BPM (9.6 s), stereo.
//   kick     a sine swept 150 -> 50 Hz with a 2 ms click, on 1 and 3 and a pickup
//   snare    a 190 Hz body plus band-limited noise, on 2 and 4
//   hat      high-passed noise, eighth notes, alternating level, panned a little
//   bass     a sustained saw-ish (three harmonics) note per bar through a one-pole low-pass: the level a compressor rides
//   pad      two detuned sines per side, a fifth above, slowly beating: stereo sustain
// Every voice is rendered with wrap-around (a tail that passes the end is added at the start), so the buffer loops
// without a click, and every sustained voice's frequency is a whole number of cycles per loop.
'use strict';

function fcmpSynthLoop(sampleRate) {
  const bpm = 100;
  const beats = 16;
  const n = Math.round(sampleRate * beats * 60 / bpm);          // 460800 at 48 kHz
  const beat = n / beats;
  const L = new Float32Array(n);
  const R = new Float32Array(n);
  const loopSeconds = n / sampleRate;
  const TAU = 2 * Math.PI;

  // xorshift32: the same noise everywhere
  let seed = 0x2f6e2b1;
  const noise = () => {
    seed ^= seed << 13; seed >>>= 0;
    seed ^= seed >>> 17;
    seed ^= seed << 5; seed >>>= 0;
    return seed / 2147483648 - 1;
  };
  const add = (at, len, gainL, gainR, voice) => {
    const start = Math.round(at);
    for (let i = 0; i < len; i += 1) {
      const s = voice(i / sampleRate, i);
      const k = (start + i) % n;
      L[k] += gainL * s;
      R[k] += gainR * s;
    }
  };

  // kick: beats 0, 2 (and 2.75 in bars 2 and 4)
  const kick = (t) => {
    const f = 50 + 100 * Math.exp(-t / 0.035);
    const phase = TAU * (50 * t + 100 * 0.035 * (1 - Math.exp(-t / 0.035)));
    const env = Math.exp(-t / 0.16) * Math.min(1, t / 0.0005);
    const click = t < 0.002 ? (1 - t / 0.002) * 0.5 : 0;
    return env * Math.sin(phase) + click * Math.sin(TAU * 1800 * t) + 0 * f;
  };
  const kickLen = Math.round(0.6 * sampleRate);
  for (let bar = 0; bar < 4; bar += 1) {
    add((bar * 4 + 0) * beat, kickLen, 0.80, 0.80, kick);
    add((bar * 4 + 2) * beat, kickLen, 0.80, 0.80, kick);
    if (bar % 2 === 1) add((bar * 4 + 2.75) * beat, kickLen, 0.55, 0.55, kick);
  }

  // snare: beats 1 and 3
  const snareLen = Math.round(0.3 * sampleRate);
  for (let bar = 0; bar < 4; bar += 1) {
    for (const b of [1, 3]) {
      let lp = 0, hp = 0, prev = 0;
      add((bar * 4 + b) * beat, snareLen, 0.50, 0.50, (t) => {
        const w = noise();
        lp += 0.55 * (w - lp);                     // low-pass
        hp = 0.90 * (hp + lp - prev);              // high-pass
        prev = lp;
        const env = Math.min(1, t / 0.0003);
        return env * (0.9 * hp * Math.exp(-t / 0.07) + 0.7 * Math.sin(TAU * 190 * t) * Math.exp(-t / 0.045));
      });
    }
  }

  // hats: eighths
  const hatLen = Math.round(0.08 * sampleRate);
  for (let e = 0; e < beats * 2; e += 1) {
    let hp = 0, prev = 0;
    const loud = e % 2 === 0 ? 0.16 : 0.09;
    add(e * beat / 2, hatLen, loud * 0.8, loud * 1.2, (t) => {
      const w = noise();
      hp = 0.6 * (hp + w - prev);
      prev = w;
      return hp * Math.exp(-t / 0.018) * Math.min(1, t / 0.0002);
    });
  }

  // bass: one note per bar (A1 E1 F1 G1 region), frequencies snapped to whole cycles per bar so each note ends at phase 0
  const barLen = 4 * beat;
  const bassNotes = [55.0, 41.2, 43.65, 49.0];
  for (let bar = 0; bar < 4; bar += 1) {
    const barSeconds = barLen / sampleRate;
    const f = Math.round(bassNotes[bar] * barSeconds) / barSeconds;
    let lp = 0;
    const len = Math.round(barLen);
    add(bar * barLen, len, 0.30, 0.30, (t, i) => {
      const s = Math.sin(TAU * f * t) + 0.5 * Math.sin(TAU * 2 * f * t) + 0.25 * Math.sin(TAU * 3 * f * t);
      const fadeIn = Math.min(1, i / (0.008 * sampleRate));
      const fadeOut = Math.min(1, (len - i) / (0.03 * sampleRate));
      lp += 0.25 * (s - lp);
      return lp * fadeIn * fadeOut * (0.75 + 0.25 * Math.exp(-t / 0.4));
    });
  }

  // pad: whole cycles per loop, so it is continuous across the loop point; a slow swell per two bars
  const cyc = (hz) => Math.round(hz * loopSeconds) / loopSeconds;
  const padL = [cyc(220.0), cyc(329.6), cyc(440.3)];
  const padR = [cyc(220.4), cyc(330.1), cyc(439.6)];
  for (let i = 0; i < n; i += 1) {
    const t = i / sampleRate;
    const swell = 0.6 + 0.4 * Math.sin(TAU * t / (loopSeconds / 2) - Math.PI / 2);
    let l = 0, r = 0;
    for (let k = 0; k < 3; k += 1) {
      l += Math.sin(TAU * padL[k] * t);
      r += Math.sin(TAU * padR[k] * t);
    }
    L[i] += 0.028 * swell * l;
    R[i] += 0.028 * swell * r;
  }

  // level: scale so the larger channel's peak is -3 dBFS
  let peak = 0;
  for (let i = 0; i < n; i += 1) peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
  const g = Math.pow(10, -3 / 20) / peak;
  for (let i = 0; i < n; i += 1) {
    L[i] *= g;
    R[i] *= g;
  }
  return { sampleRate, frames: n, left: L, right: R };
}

if (typeof module !== 'undefined') module.exports = { fcmpSynthLoop };
