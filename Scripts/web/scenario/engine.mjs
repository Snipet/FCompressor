// Scripts/web/scenario/engine.mjs: what reached the engine, read from a look of the driver (driver.mjs): the rows of
// every group are judged through these.
//
// A value has three places, and a row names all three: the Panel's (the item's `v` in the accessibility list), the
// editor's (the newest Params record it posted to the worklet, read by the port tap), and the engine's (the frame of
// the newest reply: the parameters the audio actually ran with, after the engine's own smoothing). An edit that
// reached the engine has the same number in all three.
import { PID, ROLE, sleep } from './driver.mjs';

export const near = (a, b, tolerance = 0.01) => typeof a === 'number' && typeof b === 'number'
                                                && Math.abs(a - b) <= tolerance;

// THRESHOLD in its three places, and the count of Params records so far.
export function threshold(u, s) {
  const slot = u.find(s, 'THRESHOLD', { role: ROLE.slider });
  return { panel: slot ? slot.v : null, posted: s.tap.last ? s.tap.last.v[PID.thr] : null,
           engine: s.tap.reply.thrDb, records: s.tap.n, snap: s.tap.last ? s.tap.last.snap : null };
}
// Whether THRESHOLD is `want` dB in all three places.
export const thresholdIs = (u, s, want) => {
  const t = threshold(u, s);
  return near(t.panel, want) && near(t.posted, want) && near(t.engine, want);
};
const round = (value) => (typeof value === 'number' ? Number(value.toFixed(3)) : 'nothing');
export const sayThreshold = (t) => `the Panel ${round(t.panel)}, the editor posted ${round(t.posted)}, the engine `
                                   + `runs ${round(t.engine)}`;

// The Mode in its three places: the Mode item's value (its name), the slot the editor posted, the slot the engine
// runs (and the crossfade into it: 1 when none is running).
export function mode(u, s) {
  const latch = u.find(s, 'Mode', { role: ROLE.combo });
  return { name: latch ? latch.value : null, posted: s.tap.last ? s.tap.last.v[PID.mode] : null,
           engine: s.tap.reply.modeSlot, fade: s.tap.reply.fade };
}

// Whether `test` holds on every look for `ms`: for what must NOT happen, which no event announces.
export async function holds(u, test, ms = 250) {
  const t0 = Date.now();
  let s = null;
  do {
    s = await u.look();
    if (!test(s)) return { ok: false, s };
    await sleep(40);
  } while (Date.now() - t0 < ms);
  return { ok: true, s };
}

// The largest gain reduction the engine reports in the looks of `ms` (dB, lane 0; the loop's level moves).
export async function largestGr(u, ms) {
  const t0 = Date.now();
  let most = 0;
  while (Date.now() - t0 < ms) {
    most = Math.max(most, (await u.look()).tap.reply.blockMaxGr[0]);
    await sleep(40);
  }
  return most;
}
