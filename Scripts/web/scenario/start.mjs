// Scripts/web/scenario/start.mjs: the page before START, and START.
//
//   start.idle        a new page: idle, no AudioContext yet, the editor draws behind the overlay, START is shown
//   start.list        Module.fcmpA11y() gives the Panel's list (the rest of the scenario finds its controls in it)
//   start.early-drop  a file dropped before START changes nothing (the page still names the sample loop, nothing
//                     plays and no source button can be pressed), and the page says to press START first. The file
//                     is not kept either: once START has been pressed it is not what plays
//   start.running     one press on START: the page runs, the context runs, the overlay is away and says PLAYING
//   start.telemetry   replies arrive from a configured, attached engine at the context's rate; the gate is open and the
//                     input meter shows the loop; nothing was refused, by the worklet or by the editor
//   start.audio       the worklet renders every quantum of the context's clock (its count against currentTime), in
//                     quanta of 128, two channels in and out
import { join } from 'node:path';

import { REPLY, writeTone } from './driver.mjs';
import { holds } from './engine.mjs';
import { SAMPLE, buttons } from './plays.mjs';
import { num } from './report.mjs';

export const page = 'own';

const IDLE = 'A LOOP PLAYS THROUGH THE COMPRESSOR. SOUND STARTS WHEN YOU PRESS START.';
const QUANTUM = 128;
const EARLY = 'scenario-early';                       // the file dropped before START
const KEPT_MS = 400;                                  // a file that was kept would play within this of START's end

// The context's clock and the worklet's count, read together in the page.
const CLOCK = `(async () => {
  const context = fcmpPage.context();
  const before = context.currentTime;
  const worklet = await fcmpPage.stats();
  return JSON.stringify({ before, after: context.currentTime, rate: context.sampleRate, worklet });
})()`;

export async function run({ u, row, scratch }) {
  const loaded = await u.load();
  const s0 = loaded.s || {};
  row(loaded.ok && s0.context === '' && s0.says === IDLE && s0.status.ok === 1 && s0.status.replies === 0,
      'idle', `the page is ${s0.state || 'not booted'} and says "${s0.says}"; ${s0.context === '' ? 'no' : 'an'} `
      + `AudioContext; the editor's status: ok ${s0.status ? s0.status.ok : '?'}`);
  const items = s0.a11y ? s0.a11y.items : [];
  row(s0.a11y !== null && s0.a11y !== undefined && items.some((i) => i.title === 'THRESHOLD')
      && items.every((i) => i.w > 0 && i.h > 0),
      'list', s0.a11y ? `Module.fcmpA11y(): ${items.length} items, screen ${s0.a11y.screen}, overlay `
                        + `${s0.a11y.overlay}, revision ${s0.a11y.revision}`
                      : 'the module gives no Module.fcmpA11y()');

  await u.drop(writeTone(join(scratch, 'files', `${EARLY}.wav`), { seconds: 1 }));
  const told = await u.until((s) => s.notice === 'PRESS START FIRST, THEN CHOOSE A FILE.');
  const can = told.s.can;

  const started = await u.start();
  const s1 = started.s || {};
  // The file is not kept: a kept file plays when START ends (as one dropped while START loads does), so for a while
  // after it the page does not name the file and source() is not a file. A START that did not come to run is the
  // next row's.
  const notFile = (s) => !!s.plays && s.plays.kind !== 'file' && !s.source.includes(EARLY.toUpperCase());
  const after = started.ok ? await holds(u, notFile, KEPT_MS) : null;
  const afterSaid = after === null ? ''
                  : `the page says "${after.s.source}" and source() is ${after.s.plays ? after.s.plays.kind : 'null'}`;
  row(told.ok && told.s.state === 'idle' && told.s.source === SAMPLE.line && told.s.plays === null
      && told.s.context === '' && !can.loop && !can.synth && !can.open && (after === null || after.ok),
      'early-drop', `a file dropped before START: the notice "${told.s.notice}", the page is `
      + `${told.s.state || 'not booted'} and says "${told.s.source}"; source() is `
      + `${told.s.plays === null ? 'null' : told.s.plays.kind}; ${buttons(told.s)}. `
      + (after === null ? 'START did not come to run: what plays after it was not seen'
         : after.ok ? `For ${KEPT_MS} ms after START ${afterSaid}: the file was not kept`
         : `Within ${KEPT_MS} ms of START ${afterSaid}: the file WAS kept, or nothing plays`));
  if (!row(started.ok && s1.says === 'PLAYING' && s1.button === '', 'running',
           started.ok ? `${started.ms} ms after the press: the page runs, the context is ${s1.context} at `
                        + `${s1.rate} Hz, the page says "${s1.says}"`
                      : started.why)) return;

  const linked = REPLY.configured | REPLY.attached;
  const fed = await u.until((s) => s.status.ok === 1 && (s.status.flags & (linked | REPLY.gated)) === linked
                                   && s.status.rate === s.rate && s.tap.reply.rate === s.rate
                                   && s.tap.reply.inPeak[0] > -70, 5000);
  const l = await u.ledger();
  const r = fed.s.tap.reply;
  row(fed.ok && l.worklet.ok && l.worklet.refused === 0 && l.status.refused === 0
      && l.status.posted === l.worklet.records, 'telemetry',
      `${fed.s.status.replies} replies, flags 0x${fed.s.status.flags.toString(16)}, ${r.rate} Hz, input `
      + `${num(r.inPeak[0], 1)} dBFS; ${l.status.posted} records posted, ${l.worklet.records} taken, `
      + `${l.worklet.refused} refused by the worklet, ${l.status.refused} replies refused by the editor`);

  const c0 = JSON.parse(await u.p.ev(CLOCK));
  await u.pace((s) => s.tap.replies > fed.s.tap.replies + 20, 4000);   // the page plays on for twenty more frames
  const c1 = JSON.parse(await u.p.ev(CLOCK));
  const quanta = c1.worklet.quanta - c0.worklet.quanta;
  const least = (c1.before - c0.after) * c0.rate / QUANTUM;       // the clock's quanta between the two readings,
  const most = (c1.after - c0.before) * c0.rate / QUANTUM;        // at the least and at the most
  row(quanta > 20 && quanta >= least - 2 && quanta <= most + 2 && c1.worklet.oddQuanta === 0
      && c1.worklet.lastFrames === QUANTUM && c1.worklet.inChannels === 2 && c1.worklet.outChannels === 2,
      'audio', `${quanta} quanta of ${c1.worklet.lastFrames} while the context's clock moved `
      + `${num(least, 1)} to ${num(most, 1)}; ${c1.worklet.inChannels} channels in, ${c1.worklet.outChannels} out, `
      + `latency ${c1.worklet.latency}`);
}
