// web/live/fcmp-extra.js: the print rows that have no golden, through the SHIPPED worklet in a browser (ADR-93, the
// web lead phase). The page of fcmp-extra.html; fcmp-live.js has the method and the protocol.
//
// dsp.print blesses the plugin's default setup only. The other setups are held to the shipped engine under node:
// ../expect/fcmp-extra.json, written by Tools/web/live/expect.mjs, has each Mode's default set at ECO, at HQ, at HQ
// with a lookahead budget and lookahead, and at STD and HQ at 44.1 kHz, rendered by the engine driven directly. Here
// the same rows go through ../fcmp-worklet.js in an OfflineAudioContext (at 44.1 kHz in a context of that rate, the
// program's samples played as they are) and must come out bit for bit: this browser's compiler against node's on
// the same module, in the code no blessed row runs.
//
//   expect.engine, expect.print          the expectation is this build's (fcmp-live.js, expectation())
//   expect.shape                      its Modes are the test module's, its setups the five below
//   <key> <setup> print.default.<l|r>.hash   the rows. Each also asserts that its engine was armed at the row's
//                                        first quantum with both records in, none refused, at the setup's latency
//   rows.count                           10 rows a Mode and none of the expectation's left over: guards an empty run
import { Page, environment, expectation, hashRows, render } from './fcmp-live.js';

const FORMAT = 1;                                               // Tools/web/live/expect.mjs FORMAT
const SETUPS = ['eco', 'hq', 'hqla', 'std@44100', 'hq@44100'];  // and its SETUPS: what this page must have run

const page = new Page('web.live.extra');

page.run(async () => {
  const env = await environment(page);
  const { print } = env;
  const started = performance.now();

  const expect = await expectation(page, env, 'fcmp-extra.json', FORMAT);
  if (expect === null) return;
  const setups = expect.setups || {};
  if (!page.row(JSON.stringify(expect.modes) === JSON.stringify(print.modes)
                && JSON.stringify(Object.keys(setups)) === JSON.stringify(SETUPS) && expect.frames === print.frames,
                'expect.shape', `Modes ${(expect.modes || []).join(' ')}; setups ${Object.keys(setups).join(' ')}; `
                + `${expect.frames} frames a row`)) return;

  // The setups of one rate are one group: one context a Mode where the browser can suspend it.
  const rates = [...new Set(SETUPS.map((name) => setups[name].rate))];
  let compared = 0;
  let renderMs = 0;
  for (let mode = 0; mode < print.modes.length; mode += 1) {
    const key = print.modes[mode];
    for (const rate of rates) {
      const names = SETUPS.filter((name) => setups[name].rate === rate);
      const rows = names.map((name) => ({ mode, set: 0, quality: setups[name].quality, budget: setups[name].budget,
                                          look: setups[name].look, input: 'program' }));
      const results = await render(env, rate, rows);
      rows.forEach((row, k) => {
        const name = (channel) => `${key} ${names[k]} print.default.${channel}.hash`;
        compared += hashRows(page, env, rate, row, results[k], name, expect.rows[`${key} ${names[k]}`],
                             'the engine under node');
        renderMs += results[k].laps.reduce((a, b) => a + b, 0);
      });
    }
  }
  const expected = 2 * Object.keys(expect.rows).length;
  page.row(compared === 2 * SETUPS.length * print.modes.length && compared === expected && compared > 0, 'rows.count',
           `${compared} rows compared, ${expected} in the expectation (${print.modes.length} Modes x ${SETUPS.length} `
           + 'setups x 2 channels)');
  const latency = (name) => print.latency(setups[name].quality, setups[name].budget, setups[name].rate);
  page.note(`web.live.extra latency: ${SETUPS.map((name) => `${name} ${latency(name)}`).join(', ')} samples (fcdsp's `
            + "figure for each setup, which a row's engine must report)");
  page.note(`web.live.extra time: ${compared} rows in ${((performance.now() - started) / 1000).toFixed(1)} s, `
            + `${env.contexts} contexts; the renders ${(renderMs / 1000).toFixed(1)} s`);
  page.note(`web.live.extra instances: ${env.instances} engine instances in this page load`);
});
