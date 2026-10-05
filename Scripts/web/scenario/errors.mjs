// Scripts/web/scenario/errors.mjs: the rows that hold for the whole run, printed last, and the two facts the runner
// collects for them on the way.
//
//   errors.console   no uncaught error and no error-level console line on the demo page, on any page of the run (the
//                    tab's console is kept across its documents). A renderer's warning is not an error; nor is the
//                    line the browser logs for a request of the sample loop's file that the scenario itself answered
//                    with no file (the group source does), one line a request and no more.
//   errors.records   on every page of the run that was started: the demo still plays as its last group ends (or as the
//                    group after it begins), every record the editor posted reached the worklet (posted === records,
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

// The page's ledger as a group leaves it. { ran: false } for a page where the demo was never started (START not
// pressed, or a start that did not come to play): nothing was posted to an engine there. A page that was started and
// no longer plays (the demo stopped, or failed) is `broken`, as is one that cannot be looked at.
export async function ledger(u) {
  try {
    const s = await u.look();
    if (s.state !== 'running') {
      return u.tapped() ? { ran: true, broken: `the demo no longer plays: the page is ${s.state || 'not booted'} and `
                                               + `says "${s.says}"` }
                        : { ran: false };
    }
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
  const own = u.refusals();
  row(errors.length === 0, 'console',
      errors.length === 0 ? `no uncaught error and no error-level line among the console's ${u.consoleLines()} lines`
                            + (own > 0 ? ` (but the browser's own, for the ${own} request(s) of the sample loop's file `
                                         + 'that the scenario answered with no file)' : '')
                          : `${errors.length} on the demo page, the first: ${errors.slice(0, 3).join(' || ')}`);
  const ran = ledgers.filter((l) => l.ran);
  const bad = ran.filter((l) => l.broken || !l.ok || l.posted !== l.records || l.refused !== 0 || l.editorRefused !== 0
                                || !l.linked);
  const posted = ran.reduce((sum, l) => sum + (l.posted || 0), 0);
  row(ran.length > 0 && bad.length === 0, 'records',
      bad.length > 0 ? `after ${bad[0].after}: ${JSON.stringify(bad[0])}`
      : ran.length === 0 ? 'the demo was started on no page of the run: there is nothing to count'
      : `${ran.length} page(s): ${posted} records posted, all taken by the worklet, none refused`);
}
