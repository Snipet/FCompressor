// Scripts/web/scenario/values.mjs: a value moved by the pointer, the wheel and the keys, on THRESHOLD in the Mode the
// page starts in (CLEAN: a continuous control, default -18 dB). Each row is judged in the three places of engine.mjs.
//
//   values.drag              a drag to the left lowers it: the engine runs the Panel's new value, the editor posted
//                            it as an edit (no snap), and the engine compresses more
//   values.double-click      a double click moves the engine back to the Mode's default, from the dragged value
//   values.wheel             the wheel up raises it and down lowers it, in the engine too; the page does not scroll
//   values.wheel.page        over HISTORY, which takes no wheel, the page scrolls instead and the value stays
//   values.typed             a press, then digits: the field opens; Return sets the value in the engine
//   values.typed.refused     a text that is no value: Return leaves the field open and the engine where it was
//   values.typed.cancelled   Escape closes the field and nothing is posted
//   values.typed.unit        Return opens the field on the pressed slot; a value with its unit is taken
//   values.keys              Tab shows the focus ring on the pressed slot, the arrows move its value in the engine, and
//                            the canvas keeps the document's focus
import { ROLE } from './driver.mjs';
import { holds, largestGr, near, sayThreshold, threshold, thresholdIs } from './engine.mjs';
import { num } from './report.mjs';

export const page = 'continue';

const DEFAULT_DB = -18;                               // CLEAN's THRESHOLD (Source/fcdsp/modes/clean/CleanDesc.cpp)

export async function run({ u, row }) {
  const slot = await u.item('THRESHOLD', { role: ROLE.slider });
  const [x, y] = u.centre(slot);
  const now = async () => threshold(u, await u.look());
  // The three places agree with each other, whatever the value.
  const agrees = (s) => {
    const t = threshold(u, s);
    return near(t.panel, t.posted) && near(t.panel, t.engine);
  };

  // ---- the pointer ----
  const t0 = await now();
  const quiet = await largestGr(u, 1000);
  await u.dragBy(x, y, -60, 0, 30);
  const dragged = await u.until((s) => agrees(s) && threshold(u, s).panel < t0.panel - 5);
  const t1 = threshold(u, dragged.s);
  const louder = await u.until((s) => s.tap.reply.blockMaxGr[0] > quiet + 3);
  row(dragged.ok && t1.records > t0.records && t1.snap === 0 && louder.ok, 'drag',
      `from ${t0.panel} dB by 60 px: ${sayThreshold(t1)} (${t1.records - t0.records} records, snap ${t1.snap}); `
      + `gain reduction at most ${num(quiet, 1)} dB before, ${num(louder.s.tap.reply.blockMaxGr[0], 1)} dB after`);

  await u.doubleClickAt(x, y);
  const reset = await u.until((s) => thresholdIs(u, s, DEFAULT_DB));
  const t2 = threshold(u, reset.s);
  row(reset.ok && !near(t1.engine, DEFAULT_DB, 1) && t2.records > t1.records, 'double-click',
      `from ${num(t1.engine)} dB in the engine: ${sayThreshold(t2)} (the default is ${DEFAULT_DB})`);

  // ---- the wheel ----
  for (let i = 0; i < 4; i += 1) await u.wheelAt(x, y, -40);
  const up = await u.until((s) => agrees(s) && threshold(u, s).panel > t2.panel + 0.1);
  const t3 = threshold(u, up.s);
  for (let i = 0; i < 8; i += 1) await u.wheelAt(x, y, 40);
  const down = await u.until((s) => agrees(s) && threshold(u, s).panel < t3.panel - 0.1);
  const t4 = threshold(u, down.s);
  row(up.ok && down.ok && down.s.scrollY === 0, 'wheel',
      `up: ${sayThreshold(t3)}; down: ${sayThreshold(t4)}; the page scrolled ${down.s.scrollY} px`);

  const history = await u.item(/^History, last/);
  await u.wheelAt(...u.centre(history), 120);
  const scrolled = await u.until((s) => s.scrollY > 0);
  await u.wheelAt(...u.centre(history), -120);
  const back = await u.until((s) => s.scrollY === 0);
  const t5 = threshold(u, back.s);
  row(scrolled.ok && back.ok && t5.records === t4.records && near(t5.panel, t4.panel), 'wheel.page',
      `over HISTORY the page scrolled ${scrolled.s.scrollY} px and back to ${back.s.scrollY}; THRESHOLD stays `
      + `${t5.panel}`);

  // ---- a typed value ----
  const open = (s) => s.a11y.textEntry >= 0;
  const press = async () => {
    await u.clickAt(x, y);
    return u.until((s) => s.a11y.focus === slot.id && !open(s));
  };
  const focused = await press();
  await u.type('-30');
  const field = await u.until(open);
  await u.key('Enter');
  const set = await u.until((s) => thresholdIs(u, s, -30) && !open(s));
  const t6 = threshold(u, set.s);
  row(focused.ok && field.ok && set.ok && t6.records === t5.records + 1, 'typed',
      `a press (focus ${focused.s.a11y.focus}, ring ${focused.s.a11y.focusVisible}), "-30" (the field is `
      + `${field.ok ? 'open' : 'NOT open'}), Return: ${sayThreshold(t6)}`);

  await press();
  await u.type('9z');
  await u.key('Enter');
  const kept = await holds(u, (s) => open(s) && thresholdIs(u, s, -30) && threshold(u, s).records === t6.records);
  await u.key('Escape');
  const closed = await u.until((s) => !open(s));
  row(kept.ok && closed.ok, 'typed.refused',
      `"9z" and Return: the field is ${open(kept.s) ? 'still open' : 'CLOSED'}, ${sayThreshold(threshold(u, kept.s))}; `
      + `Escape ${closed.ok ? 'closes it' : 'does NOT close it'}`);

  await press();
  await u.type('-12');
  const second = await u.until(open);
  await u.key('Escape');
  const cancelled = await u.until((s) => !open(s));
  const same = await holds(u, (s) => !open(s) && thresholdIs(u, s, -30) && threshold(u, s).records === t6.records);
  row(second.ok && cancelled.ok && same.ok, 'typed.cancelled',
      `"-12" and Escape: the field is ${open(same.s) ? 'OPEN' : 'closed'}, ${sayThreshold(threshold(u, same.s))}, `
      + `${threshold(u, same.s).records - t6.records} records posted`);

  await press();
  await u.key('Enter');
  const byReturn = await u.until(open);
  await u.type('-24 db');
  await u.key('Enter');
  const unit = await u.until((s) => thresholdIs(u, s, -24) && !open(s));
  row(byReturn.ok && unit.ok, 'typed.unit',
      `Return ${byReturn.ok ? 'opens' : 'does NOT open'} the field; "-24 db" and Return: `
      + `${sayThreshold(threshold(u, unit.s))}`);

  // ---- the keys ----
  await press();
  await u.key('Tab');
  const ring = await u.until((s) => s.a11y.focus === slot.id && s.a11y.focusVisible === 1);
  const t7 = threshold(u, ring.s);
  await u.key('ArrowRight');
  const raised = await u.until((s) => agrees(s) && threshold(u, s).panel > t7.panel);
  const t8 = threshold(u, raised.s);
  await u.key('ArrowLeft');
  const lowered = await u.until((s) => agrees(s) && near(threshold(u, s).panel, t7.panel));
  const active = await u.p.ev("document.activeElement ? document.activeElement.id : ''");
  row(ring.ok && raised.ok && lowered.ok && active === 'fcmp-canvas', 'keys',
      `Tab: the ring is ${ring.ok ? 'on THRESHOLD' : 'NOT on THRESHOLD'}; the right arrow: ${sayThreshold(t8)}; the `
      + `left arrow: ${sayThreshold(threshold(u, lowered.s))}; the document's focus is on "${active}"`);
}
