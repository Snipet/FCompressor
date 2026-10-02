// Scripts/web/scenario/errors.mjs: the rows that hold for the whole run, printed last, and the two facts the runner
// collects for them on the way.
//
//   errors.console   no uncaught error and no error-level console line on the demo page, on any page of the run (the
//                    tab's console is kept across its documents). A renderer's warning is not an error.
//   errors.records   on every page that ran: every record the editor posted reached the worklet (posted === records,
//                    read at one instant), the engine refused none, and the editor refused no reply
import { REPLY } from './driver.mjs';

// The browser and its WebGL renderer, for the run's first NOTE (a software renderer is where the pixel rows differ).
export async function browser(u) {
  const product = (await u.browser.send('Browser.getVersion')).product;
  let renderer = 'unknown';
  try {
    const info = await u.browser.send('SystemInfo.getInfo');
    renderer = (info.gpu.auxAttributes || {}).glRenderer || info.gpu.devices.map((d) => d.deviceString).join(', ');
  } catch { /* a browser that does not say */ }
  return `${product}; ${renderer}`;
}

// The page's ledger as a group leaves it: null when the page does not run (nothing was posted to an engine).
export async function ledger(u) {
  try {
    const s = await u.look();
    if (s.state !== 'running') return { ran: false };
    const l = await u.ledger();
    return { ran: true, posted: l.status.posted, records: l.worklet.records, refused: l.worklet.refused,
             lastRefusal: l.worklet.lastRefusal, ok: l.worklet.ok, editorRefused: l.status.refused,
             linked: (l.status.flags & (REPLY.configured | REPLY.attached)) === (REPLY.configured | REPLY.attached) };
  } catch (error) {
    return { ran: true, broken: String(error.message || error) };
  }
}

export const page = 'none';

export async function run(t, ledgers) {
  const { u, row } = t;
  await u.p.ev('1').catch(() => {});                  // one round trip: what the page said before now has arrived
  const errors = u.errors();
  row(errors.length === 0, 'console',
      errors.length === 0 ? `no uncaught error and no error-level line among the console's ${u.consoleLines()} lines`
                          : `${errors.length} on the demo page, the first: ${errors.slice(0, 3).join(' || ')}`);
  const ran = ledgers.filter((l) => l.ran);
  const bad = ran.filter((l) => l.broken || !l.ok || l.posted !== l.records || l.refused !== 0 || l.editorRefused !== 0
                                || !l.linked);
  const posted = ran.reduce((sum, l) => sum + (l.posted || 0), 0);
  row(ran.length > 0 && bad.length === 0, 'records',
      bad.length > 0 ? `after ${bad[0].after}: ${JSON.stringify(bad[0])}`
      : ran.length === 0 ? 'no page of the run was playing when its last group ended: there is nothing to count'
      : `${ran.length} page(s): ${posted} records posted, all taken by the worklet, none refused`);
}
