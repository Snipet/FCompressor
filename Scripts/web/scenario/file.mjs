// Scripts/web/scenario/file.mjs: a file dropped on the page (the browser's own drag events, with files made here in
// the run's scratch directory). What plays is judged by three witnesses together (plays.mjs): the page's source line,
// fcmpPage.source() and the engine's input. The file is a 1 kHz sine at -6 dBFS: every millisecond of the engine's
// input is then at -6 dBFS, which no loop holds for 0.4 s (the sample loop's level moves between -50 and -0.05 dBFS,
// the synth loop's between -42 and -3). The file is 30 s long so that it does not come round while the group runs:
// the page fades a file's two ends, and at that seam the level falls for some milliseconds.
//
//   file.drop     a dropped WAV takes the sample loop's place: the page names it, source() is the file, and the
//                 engine's input is the file's
//   file.bad      a file that is not audio leaves the source, says so in the notice, and the engine's input stays
//   file.short    a file under 0.1 s is refused the same way
//   file.loop     SAMPLE LOOP returns: the page says so, the notice is gone, source() is the sample loop and the
//                 engine's input is its again (it passes -1.5 dBFS, which the file never does)
import { join } from 'node:path';
import { writeFileSync } from 'node:fs';

import { writeTone } from './driver.mjs';
import { SAMPLE, buttons, sampleLoop, tone, toneFile } from './plays.mjs';

export const page = 'continue';

export async function run({ u, row, scratch }) {
  const files = join(scratch, 'files');
  const file = toneFile(scratch, 'scenario-tone');
  const short = writeTone(join(files, 'scenario-short.wav'), { seconds: 0.05, hz: 1000, db: file.db });
  const bad = join(files, 'scenario-not-audio.wav');
  writeFileSync(bad, 'this is not audio\n'.repeat(50));

  const before = await u.look();
  const wasSample = before.source === SAMPLE.line && !!before.plays && before.plays.kind === SAMPLE.kind;
  await u.drop(file.file);
  const named = await u.until((s) => s.source === file.line && s.notice === '', 8000);
  const plays = await tone(u, file);
  const s = plays.s;
  row(wasSample && named.ok && plays.ok && s.notice === '' && s.can.loop && s.can.synth && s.can.open, 'drop',
      `dropped while the page said "${before.source}" (source(): ${before.plays ? before.plays.kind : 'null'}): `
      + `${plays.text}; ${buttons(s)}`);

  await u.drop(bad);
  const refused = await u.until((x) => /COULD NOT READ SCENARIO-NOT-AUDIO\.WAV AS AUDIO/.test(x.notice)
                                       && /THE SOURCE IS UNCHANGED/.test(x.notice));
  const stays = await tone(u, file);
  row(refused.ok && stays.ok && stays.s.notice === refused.s.notice, 'bad',
      `the notice: "${refused.s.notice}"; ${stays.text}`);

  await u.drop(short);
  const tooShort = await u.until((x) => /SCENARIO-SHORT\.WAV IS SHORTER THAN 0\.1 S/.test(x.notice));
  const staysAgain = await tone(u, file);
  row(tooShort.ok && staysAgain.ok && staysAgain.s.notice === tooShort.s.notice, 'short',
      `the notice: "${tooShort.s.notice}"; ${staysAgain.text}`);

  await u.pressElement('fcmp-loop');
  const looped = await u.until((x) => x.source === SAMPLE.line && x.notice === '', 8000);
  const loops = await sampleLoop(u);
  const z = loops.s;
  row(looped.ok && loops.ok && z.notice === '' && !z.can.loop && z.can.synth && z.can.open, 'loop',
      `SAMPLE LOOP pressed: ${loops.text}; the notice is "${z.notice}"; ${buttons(z)}`);
}
