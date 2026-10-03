// Scripts/web/scenario/hidden.mjs: the tab hidden behind another one and shown again. The editor pulls telemetry once
// a frame that ticks, and a hidden document ticks none: the pulls must stop and the audio must not.
//
//   hidden.hidden   another tab in front (or, should that not hide it, the window minimised: driver.mjs): the
//                   document is hidden, the editor draws no frame and posts no record, and the worklet goes on
//                   rendering; the page still runs
//   hidden.shown    in front again: frames, records and replies move again, and nothing was refused meanwhile
//   hidden.edit     an edit made then reaches the engine
//   hidden.resume   the audio paused as a browser pauses it (the context suspended: nothing a user does, so it is
//                   done on the page's own context): the page says so and RESUME has the focus; a press on RESUME
//                   and the context runs, the page says PLAYING and the worklet renders again
import { ROLE } from './driver.mjs';
import { holds, near, sayThreshold, threshold } from './engine.mjs';

export const page = 'continue';

const PAUSED = 'AUDIO IS PAUSED BY THE BROWSER. PRESS RESUME.';

export async function run({ u, row }) {
  const slot = await u.item('THRESHOLD', { role: ROLE.slider });
  const visible = await u.ledger();

  let h1 = null;
  try {
    const how = await u.hide();
    const hid = await u.until((s) => s.hidden === true);
    // The frame that was under way has ended once two looks agree.
    let seen = -1;
    const settled = await u.until((s) => {
      const same = s.status.frames === seen;
      seen = s.status.frames;
      return same;
    });
    const h0 = await u.ledger();
    const quiet = await holds(u, (s) => s.hidden === true && s.status.frames === h0.status.frames
                                        && s.status.posted === h0.status.posted && s.state === 'running'
                                        && s.context === 'running', 600);
    h1 = await u.ledger();
    row(hid.ok && settled.ok && quiet.ok && h1.worklet.quanta >= h0.worklet.quanta + 30 && h1.worklet.ok
        && h1.worklet.records === h0.worklet.records, 'hidden',
        `${how}, the document is ${quiet.s.hidden ? 'hidden' : 'VISIBLE'}: `
        + `${quiet.s.status.frames - h0.status.frames} frames drawn, ${quiet.s.status.posted - h0.status.posted} `
        + `records posted, ${h1.worklet.quanta - h0.worklet.quanta} quanta rendered meanwhile; the page is `
        + `${quiet.s.state}, the context ${quiet.s.context}`);
  } finally {
    await u.show();                                   // whatever happened: no group after this one starts hidden
  }
  const shown = await u.until((s) => s.hidden === false && s.status.frames >= h1.status.frames + 10
                                     && s.status.posted >= h1.status.posted + 10
                                     && s.status.replies >= h1.status.replies + 10 && s.status.ok === 1, 8000);
  const after = await u.ledger();
  row(shown.ok && after.worklet.ok && after.worklet.refused === 0 && after.status.refused === 0
      && after.worklet.records === after.status.posted && after.worklet.quanta > h1.worklet.quanta, 'shown',
      `the document is ${shown.s.hidden ? 'HIDDEN' : 'visible'}: ${shown.s.status.frames - h1.status.frames} frames, `
      + `${shown.s.status.posted - h1.status.posted} records and ${shown.s.status.replies - h1.status.replies} replies `
      + `since; ${after.worklet.refused} refused (${after.status.posted - visible.status.posted} records over the `
      + 'whole of it)');

  const before = threshold(u, shown.s);
  await u.dragBy(...u.centre(slot), -40, 0);
  const edited = await u.until((s) => {
    const t = threshold(u, s);
    return t.panel < before.panel - 5 && near(t.panel, t.posted) && near(t.panel, t.engine);
  });
  row(edited.ok, 'edit', `a drag on THRESHOLD: ${sayThreshold(threshold(u, edited.s))}`);

  await u.p.ev('fcmpPage.context().suspend().then(() => true)');
  const paused = await u.until((s) => s.context === 'suspended' && s.says === PAUSED && s.button === 'RESUME'
                                      && !s.away && s.state === 'running');
  const focus = await u.p.ev("document.activeElement ? document.activeElement.id : ''");
  const p0 = await u.ledger();
  await u.pressElement('fcmp-start');
  const resumed = await u.until((s) => s.context === 'running' && s.says === 'PLAYING' && s.button === '' && s.away);
  const renders = await u.untilLedger((l) => l.worklet.quanta >= p0.worklet.quanta + 30 && l.worklet.ok);
  row(paused.ok && focus === 'fcmp-start' && resumed.ok && renders.ok, 'resume',
      `suspended: the page says "${paused.s.says}" and shows ${paused.s.button || 'no button'}, the focus is on `
      + `"${focus}"; a press on it: the context is ${resumed.s.context}, the page says "${resumed.s.says}", `
      + `${renders.l.worklet.quanta - p0.worklet.quanta} quanta rendered since`);
}
