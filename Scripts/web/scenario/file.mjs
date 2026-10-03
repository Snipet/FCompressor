// Scripts/web/scenario/file.mjs: a file dropped on the page (the browser's own drag events, with files made here in
// the run's scratch directory). What plays is judged at the engine: a 1 kHz sine at -1 dBFS holds the engine's input
// meter at -1 dBFS on both channels, where the built-in loop never is (its largest sample is -3 dBFS: web/loop.js).
// The file is 30 s long so that it does not come round while the group runs: the page fades a file's two ends, and at
// that seam the meter falls by half a dB and more for some milliseconds (measured: -1.57 dBFS on a 2 s file).
//
//   file.drop     a dropped WAV becomes the source: the page names it, and the engine's input is the file's
//   file.bad      a file that is not audio leaves the source, says so in the notice, and the engine's input stays
//   file.short    a file under 0.1 s is refused the same way
//   file.loop     BUILT-IN LOOP returns: the page says so, the notice is gone, the engine's input is the loop's again
import { join } from 'node:path';
import { writeFileSync } from 'node:fs';

import { writeTone } from './driver.mjs';
import { holds } from './engine.mjs';

export const page = 'continue';

const TONE_DB = -1;
const TONE_S = 30;
const LOOP_PEAK_DB = -3;
const LOOP = 'SOURCE: BUILT-IN LOOP';

export async function run({ u, row, scratch }) {
  const files = join(scratch, 'files');
  const tone = writeTone(join(files, 'scenario-tone.wav'), { seconds: TONE_S, hz: 1000, db: TONE_DB });
  const short = writeTone(join(files, 'scenario-short.wav'), { seconds: 0.05, hz: 1000, db: TONE_DB });
  const bad = join(files, 'scenario-not-audio.wav');
  writeFileSync(bad, 'this is not audio\n'.repeat(50));

  const isTone = (s) => s.tap.reply.inPeak.every((db) => Math.abs(db - TONE_DB) < 0.6);
  const level = (s) => s.tap.reply.inPeak.map((db) => db.toFixed(1)).join('/');
  const enabled = () => u.p.ev("!document.getElementById('fcmp-loop').disabled");

  const before = await u.look();
  await u.drop(tone);
  const named = await u.until((s) => s.source === 'SOURCE: SCENARIO-TONE.WAV' && s.notice === '' && isTone(s), 8000);
  const steady = await holds(u, isTone, 400);
  row(before.source === LOOP && named.ok && steady.ok && await enabled(), 'drop',
      `the page says "${named.s.source}"; the engine's input is at ${level(steady.s)} dBFS (the file: ${TONE_DB}; `
      + `the loop before it: ${level(before)})`);

  await u.drop(bad);
  const refused = await u.until((s) => /COULD NOT READ SCENARIO-NOT-AUDIO\.WAV AS AUDIO/.test(s.notice)
                                       && /THE SOURCE IS UNCHANGED/.test(s.notice));
  const stays = await holds(u, (s) => s.source === named.s.source && isTone(s), 300);
  row(refused.ok && stays.ok, 'bad',
      `the notice: "${refused.s.notice}"; the page says "${stays.s.source}", the engine's input `
      + `${level(stays.s)} dBFS`);

  await u.drop(short);
  const tooShort = await u.until((s) => /SCENARIO-SHORT\.WAV IS SHORTER THAN 0\.1 S/.test(s.notice));
  const staysAgain = await holds(u, (s) => s.source === named.s.source && isTone(s), 300);
  row(tooShort.ok && staysAgain.ok, 'short',
      `the notice: "${tooShort.s.notice}"; the page says "${staysAgain.s.source}", the engine's input `
      + `${level(staysAgain.s)} dBFS`);

  await u.pressElement('fcmp-loop');
  const isLoop = (s) => s.tap.reply.inPeak.every((db) => db < LOOP_PEAK_DB + 0.6 && db > -70);
  const looped = await u.until((s) => s.source === LOOP && s.notice === '' && isLoop(s), 8000);
  const loops = await holds(u, isLoop, 400);
  const pressable = await enabled();
  row(looped.ok && loops.ok && !pressable, 'loop',
      `the page says "${looped.s.source}", the notice is "${looped.s.notice}", the engine's input ${level(looped.s)} `
      + `dBFS, BUILT-IN LOOP is ${pressable ? 'still ENABLED' : 'disabled again'}`);
}
