// Scripts/web/scenario/quality.mjs: QUALITY and LOOKAHEAD, which rebuild the engine on the audio thread, on a page of
// its own. After each press the same latency must be in three places: the editor's status (what the facade heard),
// the worklet's own answer (the engine's) and the newest reply (its head and its frame); and the audio must go on:
// the worklet still renders (its quanta move on, it reports no fault, it refused nothing) and its output meter shows
// the loop again.
//
//   quality.hq          STD to HQ
//   quality.5ms         a 5 ms budget on HQ: 5 ms more
//   quality.20ms        20 ms
//   quality.eco         ECO, the budget kept
//   quality.std         STD, the budget kept
//   quality.off         the budget off: the latency the page started with
//   quality.settings    the settings screen's QUALITY cells do the same, and its LATENCY row says the engine's number
//
// The numbers at 48 kHz in the Mode the page starts in (CLEAN): 4 samples at STD, 61 at HQ, none at ECO, and the
// budget on top (240 and 960). At another rate the rows hold the budget's part to rate x time and say so.
import { OVERLAY, PID, ROLE } from './driver.mjs';

export const page = 'new';

const AT_48K = { Std: 4, HQ: 61, Eco: 0 };
const BUDGET_MS = { Off: 0, '5 ms': 5, '20 ms': 20 };
const QUANTA = 30;                                    // the worklet must render this many more after a change

export async function run({ u, row, note }) {
  // The editor's status and the worklet's counters at one instant, and a look.
  const read = async () => {
    const l = await u.ledger();
    const s = await u.look();
    return { s, status: l.status, worklet: l.worklet, reply: s.tap.reply,
             latencies: [l.status.latency, l.worklet.latency, s.tap.reply.latency, s.tap.reply.frameLatency] };
  };
  // Reads until `test` holds: through the driver's waiting, so the wait counts as every other does.
  const until = async (test, ms = 6000) => {
    const w = await u.untilOn(read, test, ms);
    return { ok: w.ok, r: w.v };
  };
  const start = await read();
  const rate = start.s.rate;
  const exact = rate === 48000;
  if (!exact) note('rate', `the context runs at ${rate} Hz: the latencies are judged by their budget's part only`);
  const state = { quality: 'Std', budget: 'Off', base: { Std: start.worklet.latency } };
  const budgetSamples = (name) => Math.round(BUDGET_MS[name] * rate / 1000);

  // A press on `cell` of `group`; then the latency, the same in all places, is `want` (null: only the same and not
  // what it was), and the audio goes on.
  const change = async (name, group, cell) => {
    const before = await read();
    await u.press(cell, { parent: group, role: ROLE.radio });
    if (group === 'Quality') state.quality = cell;
    else state.budget = cell;
    const known = exact ? AT_48K[state.quality] : state.base[state.quality];
    const want = known === undefined ? null : known + budgetSamples(state.budget);
    const changed = await until((r) => r.latencies.every((l) => l === r.latencies[0])
                                       && (want === null ? r.latencies[0] !== before.latencies[0]
                                                         : r.latencies[0] === want)
                                       && u.value(r.s, group) === cell);
    if (known === undefined) state.base[state.quality] = changed.r.latencies[0] - budgetSamples(state.budget);
    const on = await until((r) => r.worklet.quanta >= changed.r.worklet.quanta + QUANTA && r.reply.outPeak[0] > -60
                                  && r.s.context === 'running');
    const r = on.r;
    const posted = r.s.tap.last;
    row(changed.ok && on.ok && r.worklet.ok && r.worklet.refused === 0 && r.status.refused === 0
        && r.worklet.records === r.status.posted,
        name, `${group} ${cell}: latency ${before.latencies[0]} -> ${r.latencies.join('/')} in the status, the `
        + `worklet, the reply and its frame${want === null ? '' : ` (${want} expected)`}; the editor posted quality `
        + `${posted.v[PID.quality]}, budget ${posted.v[PID.labudget]}; ${r.worklet.quanta - before.worklet.quanta} `
        + `quanta since, the output at ${r.reply.outPeak[0].toFixed(1)} dBFS, ${r.worklet.refused} refused`);
    return r;
  };

  await change('hq', 'Quality', 'HQ');
  await change('5ms', 'Lookahead budget', '5 ms');
  await change('20ms', 'Lookahead budget', '20 ms');
  await change('eco', 'Quality', 'Eco');
  await change('std', 'Quality', 'Std');
  const end = await change('off', 'Lookahead budget', 'Off');
  if (end.latencies[0] !== start.latencies[0]) {
    row(false, 'off.returns', `the page started with latency ${start.latencies[0]} and is back at STD, OFF with `
                              + `${end.latencies[0]}`);
  }

  // ---- the settings screen ----
  const line = (s) => u.value(s, 'Latency', { role: ROLE.text });
  await u.press('Settings');
  const opened = await u.until((s) => s.a11y.overlay === OVERLAY.settings && line(s) !== null);
  const before = line(opened.s);
  await u.press('HQ', { parent: 'Quality', role: ROLE.radio });
  const hq = await until((r) => r.latencies.every((l) => l === r.latencies[0]) && r.latencies[0] !== end.latencies[0]
                                && line(r.s).startsWith(`${r.latencies[0]} SAMPLES`));
  await u.press('Std', { parent: 'Quality', role: ROLE.radio });
  const std = await until((r) => r.latencies.every((l) => l === end.latencies[0])
                                 && line(r.s).startsWith(`${end.latencies[0]} SAMPLES`));
  await u.key('Escape');
  const closed = await u.until((s) => s.a11y.overlay === OVERLAY.none);
  row(opened.ok && hq.ok && std.ok && closed.ok && before.startsWith(`${end.latencies[0]} SAMPLES`), 'settings',
      `LATENCY says "${before}"; HQ in settings: "${line(hq.r.s)}" (the engine: ${hq.r.latencies.join('/')}); STD: `
      + `"${line(std.r.s)}" (${std.r.latencies.join('/')})`);
}
