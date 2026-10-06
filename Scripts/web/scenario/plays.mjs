// Scripts/web/scenario/plays.mjs: what plays, as three witnesses say it together: the page (its source line),
// fcmpPage.source() (the buffer that plays: its kind, its name, its frames, its rate) and the engine (its input). The
// groups source, file and start judge a source through these, so no row passes on the page's word alone, and none on
// a level alone.
//
// The engine's input is read from the 1 ms columns of its replies (the driver's u.hear and u.levels): each column
// holds the largest input sample of one millisecond, both channels, so nothing is missed between two looks. A frame's
// own meter has fallen by the time a look reads it (40 ms release).
//
// A loop is told by comparing those columns with the loop itself. The scripted user makes the loop as the page does,
// from the repository's own files and not the site's: web/sample.js reads web/audio/loop.wav and fits it to the
// context's rate, and web/loop.js makes the synth loop. Of every millisecond of it, wherever a column may begin, it
// takes the largest sample of both sides. Then it looks for the place in the loop where the columns heard begin (the
// loop is periodic, and a source may begin anywhere against the engine's columns), and at that place every column
// heard must be the loop's own level within 0.01 dB. Measured in headless Chrome 154 at 48 kHz: the page's buffer
// reaches the engine sample for sample, and no column was off by 0.00001 dB, over whole loops and after a change of
// source alike. So 0.01 dB is far from what the right page gives, and a gain of 0.998 fails every millisecond.
//   0.4 s of a loop   tells the loop from the other loop, from a file and from the same loop at another level: no
//                     0.4 s of one loop is 0.4 s of the other (laid on each other at any millisecond of both, 394
//                     of 400 columns differ at least, by up to 47 dB)
//   one whole loop    every millisecond of the loop, so also its two ends (a fade there is 5 ms of it). The listening
//                     begins at least 0.25 s after the source began and lasts one loop, so its end lies in the loop's
//                     second pass: a source that is not looped is silent there
// And the two sides. A column is the larger of both. One side played on both channels is told by the columns all the
// same (of any 0.4 s with one side on both, 230 columns and more are off in the sample loop and 150 and more in the
// synth loop), but two sides that changed places are not. The frame's two input meters tell them: over the sample
// loop the right meter stands more than 1 dB over the left in 35 to 40 % of the replies, and the left over the right
// in under 2.5 % (under node, the fitted loop through the engine's meter, read every 3 to 15 blocks; in Chrome, 180
// and 2 of 465 replies). A whole loop of the sample loop is asked for 15 % at least and 10 % at most. The synth
// loop's sides are nearly the same (under 3 % either way), so nothing is asked of them: its two sides in each
// other's place are not told here. The page makes both loops' buffers with one function, and the sample loop tells.
//
// The tone is a 1 kHz sine at -6 dBFS that toneFile() writes: every millisecond of it has that level, and a loop's
// level moves by 16 dB and more within any 0.4 s.
//
// And how a source begins, for the row about two presses in quick succession (rise() below). The sample loop begins
// with a kick: its first millisecond peaks at -1.7 dBFS. A source that takes another's place fades in over 30 ms, so
// the synth loop, which never passes -3 dBFS, is under -23 dBFS for its first 3 ms whatever its samples are. After a
// file at -70 dBFS (toneFile() with that level), the first milliseconds of what comes next are seen as they are.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

import { synthLoop as makeSynthLoop } from '../../../web/loop.js';
import { fitLoop, readWav } from '../../../web/sample.js';
import { writeTone } from './driver.mjs';

// What the page says of the two loops (web/main.js SAY), and the notices of a sample loop that does not come.
export const SAMPLE = { line: 'SOURCE: SAMPLE LOOP', kind: 'sample', name: 'SAMPLE LOOP' };
export const SYNTH = { line: 'SOURCE: SYNTH LOOP', kind: 'synth', name: 'SYNTH LOOP' };
export const NOTICE = { loading: 'LOADING THE SAMPLE LOOP.',
                        lost: 'THE SAMPLE LOOP DID NOT LOAD. THE SYNTH LOOP PLAYS INSTEAD.',
                        unchanged: 'THE SAMPLE LOOP DID NOT LOAD. THE SOURCE IS UNCHANGED.' };
// The sample loop's file (web/audio/loop.wav: 341,420 frames at 44100 Hz) as one period at the context's rate, and
// the synth loop's sixteen beats of 0.6 s (web/loop.js).
export const sampleFrames = (rate) => Math.round(341420 * rate / 44100);
export const synthFrames = (rate) => 16 * Math.round(rate * 60 / 100);

const LOOP_FILE = join(import.meta.dirname, '..', '..', '..', 'web', 'audio', 'loop.wav');
const LOOP_MS = 400;                                  // the short listening: this much of a loop
const WITHIN_DB = 0.01;                               // a column heard is the loop's own within this
const FLOOR_DB = -200;                                // the engine's level of digital silence
// The sample loop's two sides, by the replies of one whole loop: the right meter over the left in at least RIGHT of
// them, the left over the right in at most LEFT, and REPLIES of them at least (a frame a reply: some 460 at 60 Hz).
const SIDES = { replies: 20, right: 0.15, left: 0.10 };
const TONE_DB = -6;
const TONE_WITHIN_DB = 0.2;
export const QUIET_DB = -70;                          // a tone so quiet that what comes after it is seen to come
const QUIET_WITHIN_DB = 3;                            // 16 bits hold a level that low to a third of a dB
const QUIET_MS = 100;
const RISE_OVER_DB = -50;                             // the first column above this is where the next source begins
export const RISE_UNDER_DB = -23;                     // 0.708 (-3 dBFS) times 3 ms of a 30 ms fade
export const RISE_MS = 400;                           // the columns kept for rise(): the presses, 30 ms, and room
const TONE_S = 30;                                    // so long that its seam (the page's 5 ms fades) never comes round
const TONE_MS = 400;
// A change of source is over this long after the page names the new one: the old source fades for 30 ms, the new one
// fades in for 30 ms more, and a reply's columns are a frame or two behind the audio.
const SWAP_MS = 250;
const SLACK_MS = 5000;                                // the columns come at the audio's pace: this much later at most

// The tone as a file of the run's scratch directory, for a drop: { file, line, name, db }.
export function toneFile(scratch, stem, db = TONE_DB) {
  return { file: writeTone(join(scratch, 'files', `${stem}.wav`), { seconds: TONE_S, hz: 1000, db }),
           line: `SOURCE: ${stem.toUpperCase()}.WAV`, name: `${stem.toUpperCase()}.WAV`, db };
}

// ---- a loop, and the columns the engine would make of it ------------------------------------------------------------
// The loop `kind` ('sample' or 'synth') at `rate`, made once a rate: { frames, rate, peak (of every frame, the larger
// of its two sides), stretches (below) }.
const loops = new Map();
function loopAt(kind, rate) {
  const key = `${kind} ${rate}`;
  if (!loops.has(key)) {
    const loop = kind === 'sample' ? fitLoop(readWav(readFileSync(LOOP_FILE)), rate) : makeSynthLoop(rate);
    const peak = new Float32Array(loop.frames);
    for (let i = 0; i < loop.frames; i += 1) peak[i] = Math.max(Math.abs(loop.left[i]), Math.abs(loop.right[i]));
    loops.set(key, { frames: loop.frames, rate, peak, stretches: new Map() });
  }
  return loops.get(key);
}
// The level in dBFS of every stretch of `length` frames of the loop, by the frame it begins at: what a column of that
// length reads there. The loop is periodic.
function stretchesOf(loop, length) {
  if (!loop.stretches.has(length)) {
    const { frames, peak } = loop;
    const db = new Float32Array(frames);
    for (let i = 0; i < frames; i += 1) {
      let most = 0;
      for (let k = 0; k < length; k += 1) {
        const x = peak[i + k < frames ? i + k : (i + k) % frames];
        if (x > most) most = x;
      }
      db[i] = most > 0 ? Math.max(FLOOR_DB, 20 * Math.log10(most)) : FLOOR_DB;
    }
    loop.stretches.set(length, db);
  }
  return loop.stretches.get(length);
}
const gcd = (a, b) => (b === 0 ? a : gcd(b, a % b));

// The columns `levels` (dBFS, in order, none missing) against `loop`, at the place in the loop where they fit:
// { differ (how many are off by more than WITHIN_DB), worst (the largest difference, dB), first (the first column
// that is off; -1 when none is), heard and own (the level heard there and the loop's own) }.
// The engine cuts column j at frame floor(j * rate / 1000) of its own count. At a rate of whole frames a millisecond
// every column has the same length; at 44.1 kHz the lengths are 44 and 45 frames and repeat every ten columns, and
// which of the ten the first column heard is cannot be known: each is tried. The place is looked for with a few
// columns spread over the listening, at every frame of the loop (the sum of their differences, each counted as 3 dB
// at most). The sixteen nearest places are then judged on every column: the first that fits is the answer, and when
// none fits, the nearest of all. Sixteen, because the places a few frames before the right one can have its very
// sum: a column's largest sample is often not at its edge, and the column a frame earlier then reads the same. Of
// equal places the earliest are kept. At 48 kHz, of the sample loop's 371,614 places, 13 have four or five such
// places before them for a listening of 0.4 s (4 for a whole loop; the synth loop has none): with four places kept,
// the right loop was not found there.
const PROBES = 48;
const PROBE_MOST_DB = 3;
const PLACES = 16;
function against(levels, loop) {
  const { frames, rate } = loop;
  const heard = levels.map((level) => (level > FLOOR_DB ? level : FLOOR_DB));
  const step = Math.max(1, Math.floor(heard.length / PROBES));
  let nearest = null;                                 // { sum, found } of the nearest place that does not fit
  for (let grid = 0; grid < 1000 / gcd(rate, 1000); grid += 1) {
    // The columns when the first one heard is the engine's column `grid`: for each, the frame it begins at after
    // the first, and the loop's levels for a column of its length.
    const cut = (j) => Math.floor((grid + j) * rate / 1000) - Math.floor(grid * rate / 1000);
    const begins = heard.map((level, j) => cut(j) % frames);
    const own = heard.map((level, j) => stretchesOf(loop, cut(j + 1) - cut(j)));
    const places = [];                                // { at, sum }, the nearest first
    for (let at = 0; at < frames; at += 1) {
      const bound = places.length === PLACES ? places[PLACES - 1].sum : Infinity;
      let sum = 0;
      for (let j = 0; j < heard.length && sum < bound; j += step) {
        sum += Math.min(PROBE_MOST_DB, Math.abs(heard[j] - own[j][(at + begins[j]) % frames]));
      }
      if (sum >= bound) continue;
      places.push({ at, sum });
      places.sort((a, b) => a.sum - b.sum);
      if (places.length > PLACES) places.pop();
    }
    for (const { at, sum } of places) {
      const found = { differ: 0, worst: 0, first: -1, heard: 0, own: 0 };
      for (let j = 0; j < heard.length; j += 1) {
        const level = own[j][(at + begins[j]) % frames];
        const off = Math.abs(heard[j] - level);
        if (off > found.worst) found.worst = off;
        if (off <= WITHIN_DB) continue;
        found.differ += 1;
        if (found.first < 0) Object.assign(found, { first: j, heard: heard[j], own: level });
      }
      if (found.differ === 0) return found;
      if (nearest === null || sum < nearest.sum) nearest = { sum, found };
    }
  }
  return nearest.found;
}

// ---- listening ------------------------------------------------------------------------------------------------------
// The engine's input over the next `ms` of audio, from the end of a change of source: { ok, n, min, max, lost, levels
// (the first `ms` columns, one by one), replies, left, right (the two input meters, as the library's tap counts
// them), s (the last look) }. `ok`: the demo played, that many columns came and none was lost between them. A demo
// that does not play is heard at once: nothing came, and nothing is waited for.
async function hear(u, ms) {
  const nothing = { n: 0, min: NaN, max: NaN, lost: 0, levels: [], replies: 0, left: 0, right: 0 };
  const playing = (s) => s.state === 'running' && s.tap !== null;
  if (!u.tapped() || !playing(await u.look())) return { ...nothing, ok: false, s: await u.look() };
  await u.hear(SWAP_MS, ms);
  // A listening takes its whole time by design: it is no wait for an outcome.
  const r = await u.pace((s) => !playing(s) || s.tap.heard.n >= ms, ms + SWAP_MS + SLACK_MS);
  if (!r.s || !playing(r.s)) return { ...nothing, ok: false, s: r.s };
  const heard = r.s.tap.heard;
  const levels = await u.levels();
  return { ...heard, min: heard.min ?? NaN, max: heard.max ?? NaN, levels,
           ok: r.ok && heard.n >= ms && heard.lost === 0 && levels.length === ms, s: r.s };
}
const lostSaid = (h) => (h.lost > 0 ? `; ${h.lost} ms of it were NOT delivered` : '');
const db = (level) => (Number.isFinite(level) ? level.toFixed(2) : 'no level');
// What the engine heard, when it heard nothing: a row says why it has no level to give.
const silent = (h) => (h.n === 0 ? ' (the demo does not play: no input was heard)' : '');

// Whether the page's word and its buffer are `want`'s: the source line, and source()'s kind, name, frames and rate.
const named = (s, want, frames) => !!s && s.source === want.line && !!s.plays && s.plays.kind === want.kind
                                   && s.plays.name === want.name && frames(s.plays.frames, s.rate)
                                   && s.plays.sampleRate === s.rate;
const said = (s) => (s && s.plays ? `the page says "${s.source}"; source() is ${s.plays.kind}, "${s.plays.name}", `
                                    + `${s.plays.frames} frames at ${s.plays.sampleRate} Hz`
                                  : `the page says "${s ? s.source : '?'}"; source() is null`);

// The engine's input against the loop `want` (SAMPLE or SYNTH), whose frames at a rate are `frames(rate)`: for 0.4 s,
// or with `whole` for one whole loop. { ok, h (what hear() gave), rate, text }.
async function loopHeard(u, want, frames, whole) {
  const rate = (await u.look()).rate || 48000;        // no context: nothing plays, and hear() says so
  const h = await hear(u, whole ? Math.ceil(frames(rate) * 1000 / rate) : LOOP_MS);
  const found = h.ok ? against(h.levels, loopAt(want.kind, rate)) : null;
  const ms = h.ok ? h.levels.length : h.n;
  const over = `the engine's input ${whole ? `over one whole loop of ${ms} ms` : `for ${ms} ms`}`;
  const text = found === null ? `${over} was not all heard`
             : found.differ === 0 ? `${over} is the ${want.kind} loop's own, millisecond by millisecond (within `
                                    + `${found.worst.toFixed(3)} dB; it peaked at `
                                    + `${db(h.levels.reduce((most, level) => Math.max(most, level)))} dBFS)`
             : `${over} is NOT the ${want.kind} loop's: where it is nearest the loop, ${found.differ} of `
               + `${h.levels.length} ms differ by more than ${WITHIN_DB} dB, by up to ${found.worst.toFixed(2)}; the `
               + `first of them ${found.first} ms into the listening, where the loop has ${db(found.own)} dBFS and `
               + `the engine heard ${db(found.heard)}`;
  return { ok: found !== null && found.differ === 0, h, rate, text: `${text}${silent(h)}${lostSaid(h)}` };
}

// The sample loop plays: the page names it, source() is it as one period at the context's rate, and 0.4 s of the
// engine's input are 0.4 s of the loop, as the repository's file gives it at that rate. With `whole`, one whole loop
// of the input is the loop, to its last millisecond and on into its second pass, and the two input meters show the
// file's two sides. { ok, text, s }.
export async function sampleLoop(u, { whole = false } = {}) {
  const is = await loopHeard(u, SAMPLE, sampleFrames, whole);
  const h = is.h;
  const sides = !whole || (h.replies >= SIDES.replies && h.right >= SIDES.right * h.replies
                           && h.left <= SIDES.left * h.replies);
  const sidesSaid = whole ? `; of the ${h.replies} replies in it, the right input meter stood over the left in `
                            + `${h.right} and the left over the right in ${h.left} (the file's two sides: at least `
                            + `${SIDES.right * 100} % and at most ${SIDES.left * 100} %)` : '';
  return { ok: named(h.s, SAMPLE, (frames, at) => frames === sampleFrames(at)) && is.ok && sides, s: h.s,
           text: `${said(h.s)} (the file as one period there: ${sampleFrames(is.rate)}); ${is.text}${sidesSaid}` };
}

// The synth loop plays: the page names it, source() is it, and 0.4 s of the engine's input are 0.4 s of the loop, as
// web/loop.js makes it at the context's rate. With `whole`, one whole loop of the input is the loop, on into its
// second pass. { ok, text, s }.
export async function synthLoop(u, { whole = false } = {}) {
  const is = await loopHeard(u, SYNTH, synthFrames, whole);
  return { ok: named(is.h.s, SYNTH, (frames, at) => frames === synthFrames(at)) && is.ok, s: is.h.s,
           text: `${said(is.h.s)}; ${is.text}` };
}

// The tone of toneFile() plays: the page names the file, source() is a file of its length at the context's rate, and
// every millisecond of the engine's input for 0.4 s is at the tone's level. { ok, text, s }.
export async function tone(u, want) {
  const h = await hear(u, TONE_MS);
  const level = h.ok && h.min >= want.db - TONE_WITHIN_DB && h.max <= want.db + TONE_WITHIN_DB;
  const file = { ...want, kind: 'file' };
  return { ok: named(h.s, file, (frames, at) => Math.abs(frames - TONE_S * at) <= 2) && level, s: h.s,
           text: `${said(h.s)}; every millisecond of the engine's input for ${h.n} ms was at ${db(h.min)} to `
                 + `${db(h.max)} dBFS (the file: ${want.db})${silent(h)}${lostSaid(h)}` };
}

// The quiet tone of toneFile(scratch, stem, QUIET_DB) plays: the page names the file, source() is it, and every
// millisecond of the engine's input for 0.1 s is at its level, far under any loop's. { ok, text, s }.
export async function quiet(u, want) {
  const h = await hear(u, QUIET_MS);
  const level = h.ok && h.min >= want.db - QUIET_WITHIN_DB && h.max <= want.db + QUIET_WITHIN_DB;
  const file = { ...want, kind: 'file' };
  return { ok: named(h.s, file, (frames, at) => Math.abs(frames - TONE_S * at) <= 2) && level, s: h.s,
           text: `${said(h.s)}; the engine's input for ${h.n} ms was at ${db(h.min)} to ${db(h.max)} dBFS (the file: `
                 + `${want.db})${silent(h)}${lostSaid(h)}` };
}

// How the engine's input rose from the quiet tone to the source after it. `levels` are the columns since just before
// the change (u.levels()). { ok, first (the levels of the first two milliseconds of the rise, dBFS), text }. `ok`:
// both are under -23 dBFS, which the synth loop is while it fades in, and which a loop that begins un-faded is not
// (the sample loop's first millisecond at a third of its level is at -11 dBFS).
export function rise(levels) {
  const at = levels.findIndex((level) => level > RISE_OVER_DB);
  const first = at < 0 ? [] : levels.slice(at, at + 2);
  if (first.length < 2) {
    return { ok: false, first, text: `the engine's input did NOT rise above ${RISE_OVER_DB} dBFS in the `
                                     + `${levels.length} ms after the presses` };
  }
  const ok = first.every((level) => level < RISE_UNDER_DB);
  return { ok, first, text: `the engine's input rose from the file's level with ${first.map(db).join(' and ')} dBFS `
                            + `in its first two milliseconds (${ok ? 'under' : 'NOT under'} ${RISE_UNDER_DB}: a `
                            + 'source that fades in)' };
}

// The three source buttons as a row says them.
export const buttons = (s) => (s && s.can ? `SAMPLE LOOP ${s.can.loop ? 'enabled' : 'disabled'}, SYNTH LOOP `
                                            + `${s.can.synth ? 'enabled' : 'disabled'}, OPEN `
                                            + `${s.can.open ? 'enabled' : 'disabled'}` : 'no buttons');
