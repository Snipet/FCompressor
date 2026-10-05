// Scripts/web/scenario/plays.mjs: what plays, as three witnesses say it together: the page (its source line),
// fcmpPage.source() (the buffer that plays: its kind, its name, its frames, its rate) and the engine (its input). The
// groups source, file and start judge a source through these, so no row passes on the page's word alone, and none on
// a level alone.
//
// The engine's input is read from the 1 ms columns of its replies (the driver's u.hear and a look's tap.heard): each
// column holds the largest input sample of one millisecond, both channels, so nothing is missed between two looks. A
// frame's own meter has fallen by the time a look reads it (40 ms release), and one look of it can lie between two
// sources' levels by accident: the loops are not told apart by it.
//
// What tells the three sources apart there, measured on the loops themselves under node (web/sample.js and
// web/loop.js at 44.1, 48 and 96 kHz):
//   the sample loop   its largest sample is at -0.05 dBFS (-0.04 in the file, before it is fitted to 48 kHz); it
//                     passes -1.5 dBFS at least every 0.94 s and -2.5 dBFS at least every 0.49 s
//   the synth loop    its largest sample is at -3 dBFS, by its own scaling: it never passes it. It reaches -6 dBFS
//                     at least every 1.2 s, and -3.5 dBFS six times a loop
//   the tone          a 1 kHz sine at -6 dBFS that toneFile() writes: every millisecond of it has that level. A loop's
//                     level moves by 16 dB and more within any 0.4 s
// So an input that passes -1.5 dBFS is the sample loop's and neither the synth loop's nor the tone's; one that stays
// under -2.5 dBFS for 0.5 s is not the sample loop's; and one that holds -6 dBFS for 0.4 s is the tone's.
//
// And how a source begins, for the row about two presses in quick succession (rise() below). The sample loop begins
// with a kick: its first millisecond peaks at -1.7 dBFS. A source that takes another's place fades in over 30 ms, so
// the synth loop, which never passes -3 dBFS, is under -23 dBFS for its first 3 ms whatever its samples are. After a
// file at -70 dBFS (toneFile() with that level), the first milliseconds of what comes next are seen as they are.
import { join } from 'node:path';

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

const SAMPLE_PASSES_DB = -1.5;                        // the sample loop passes it within a second; no other source does
const SAMPLE_PEAK_DB = -0.15;                         // over a whole loop its largest sample lies above this, under 0
const SYNTH_UNDER_DB = -2.5;                          // the synth loop never comes up to it
const SYNTH_PEAK_DB = -3.5;                           // over a whole loop its largest sample lies above this
const SYNTH_SOUNDS_DB = -6.5;                         // and above this within any 1.2 s
const SYNTH_SHORT_MS = 1300;
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

// The engine's input over the next `ms` of audio, from the end of a change of source: { ok, n, min, max, lost, s (the
// last look) }. `ok`: the demo played, that many columns came and none was lost between them. With `enough`, the
// listening ends as soon as enough(heard) holds, and that is `ok` too. A demo that does not play is heard at once:
// nothing came, and nothing is waited for.
async function hear(u, ms, enough = null) {
  const nothing = { n: 0, min: NaN, max: NaN, lost: 0 };
  const playing = (s) => s.state === 'running' && s.tap !== null;
  if (!u.tapped() || !playing(await u.look())) return { ...nothing, ok: false, s: await u.look() };
  await u.hear(SWAP_MS);
  const full = (s) => s.tap.heard.n >= ms || (enough !== null && s.tap.heard.n > 0 && enough(s.tap.heard));
  const over = (s) => !playing(s) || full(s);
  // A listening with no early end takes its whole time by design: it is no wait for an outcome.
  const bound = ms + SWAP_MS + SLACK_MS;
  const r = enough === null ? await u.pace(over, bound) : await u.until(over, bound);
  if (!r.s || !playing(r.s)) return { ...nothing, ok: false, s: r.s };
  const heard = r.s.tap.heard;
  return { n: heard.n, min: heard.min ?? NaN, max: heard.max ?? NaN, lost: heard.lost,
           ok: r.ok && full(r.s) && heard.lost === 0, s: r.s };
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

// The sample loop plays: the page names it, source() is it as one period at the context's rate, and the engine's
// input passes -1.5 dBFS within one loop. With `whole`, the input is heard for one whole loop, and its largest sample
// is the loop's own: above -0.15 dBFS and under full scale, so the page plays the file at the file's level (a gain of
// 0.98 would fail it). { ok, text, s }.
export async function sampleLoop(u, { whole = false } = {}) {
  const rate = (await u.look()).rate || 48000;        // no context: nothing plays, and hear() says so
  const loopMs = Math.ceil(sampleFrames(rate) * 1000 / rate);
  const h = await hear(u, loopMs, whole ? null : (heard) => heard.max > SAMPLE_PASSES_DB);
  const passed = h.ok && (whole ? h.max > SAMPLE_PEAK_DB && h.max < 0 : h.max > SAMPLE_PASSES_DB);
  const level = whole ? `over one whole loop of ${h.n} ms peaked at ${db(h.max)} dBFS (the loop's own largest sample: `
                        + `above ${SAMPLE_PEAK_DB}, under 0)`
              : passed ? `passed ${SAMPLE_PASSES_DB} dBFS after ${h.n} ms (${db(h.max)})`
              : `did NOT pass ${SAMPLE_PASSES_DB} dBFS in ${h.n} ms of a loop's ${loopMs} (${db(h.max)})`;
  return { ok: named(h.s, SAMPLE, (frames, at) => frames === sampleFrames(at)) && passed, s: h.s,
           text: `${said(h.s)} (the file as one period there: ${sampleFrames(rate)}); the engine's input ${level}`
                 + `${silent(h)}${lostSaid(h)}` };
}

// The synth loop plays: the page names it, source() is it, and the engine's input stays under -2.5 dBFS while it
// sounds. With `whole`, for one whole loop, whose largest sample is the synth loop's own -3 dBFS; else for 1.3 s:
// the sample loop never stays under it that long. { ok, text, s }.
export async function synthLoop(u, { whole = false } = {}) {
  const rate = (await u.look()).rate || 48000;
  const ms = whole ? Math.ceil(synthFrames(rate) * 1000 / rate) : SYNTH_SHORT_MS;
  const h = await hear(u, ms);
  const floor = whole ? SYNTH_PEAK_DB : SYNTH_SOUNDS_DB;
  const level = h.ok && h.max <= SYNTH_UNDER_DB && h.max > floor;
  return { ok: named(h.s, SYNTH, (frames, at) => frames === synthFrames(at)) && level, s: h.s,
           text: `${said(h.s)}; the engine's input over ${whole ? 'one whole loop of ' : ''}${h.n} ms peaked at `
                 + `${db(h.max)} dBFS (the synth loop: above ${floor}, never above ${SYNTH_UNDER_DB}`
                 + `${whole ? '' : ', where the sample loop is every 0.5 s'})${silent(h)}${lostSaid(h)}` };
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
