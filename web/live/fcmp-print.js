// web/live/fcmp-print.js: dsp.print's blessed rows through the SHIPPED worklet in a browser (ADR-93, the web lead
// phase). The page of fcmp-print.html; fcmp-live.js has the method and the protocol.
//
// The native probe dsp.print renders a 4 s program through each Mode at four parameter sets and blesses a hash per
// channel: 8 rows a Mode in tests/golden/base/modes/<key>/dsp.print.txt, which the gate serves verbatim as
// golden/<key>.dsp.print.txt. Here the same program goes through ../fcmp-worklet.js over ../fcmp-engine.wasm in an
// OfflineAudioContext at the plugin's default setup (STD, no lookahead, 48 kHz), and every blessed row must come out
// bit for bit: the engine's arithmetic inside process(), in this browser's compiler, for every Mode.
//
//   program.hash                     the input is the program the goldens were made from (PrintProgram.h's, hashed
//                                    natively): guards a wrong test module
//   <key> print.<set>.<l|r>.hash     the blessed rows. Each also asserts that its engine was armed at the row's first
//                                    quantum with both records in, none refused, at the setup's latency
//   <key> golden                     only when a Mode has no golden file, or one that is not 8 exact print rows
//   rows.count                       8 rows a Mode and no golden row left over: guards an empty run
//   <key> quantum.<l|r>.hash         the default set again at a render quantum of 320 frames, which the worklet hands
//                                    to the engine in pieces of 128, 128 and 64 (its other path). Web Audio 1.1's
//                                    renderSizeHint; a NOTE where the browser renders in quanta of 128 only
import { Page, QUANTUM, environment, goldenRows, hashRows, render, textOf } from './fcmp-live.js';

const PROGRAM_HASH = '2e3621bff17e9df5 9960d5f0d72e0b18';     // PrintProgram.h's L and R, from a native build
const RATE = 48000;
const STD = { quality: 1, budget: 0, look: -1 };
const ROWS_PER_MODE = 8;
const HINT = 320;                                               // frames: neither 128 nor a multiple of it

const page = new Page('web.live.print');

page.run(async () => {
  const env = await environment(page);
  const { print } = env;
  const started = performance.now();

  const programHash = `${print.hash(print.programL)} ${print.hash(print.programR)}`;
  page.row(programHash === PROGRAM_HASH, 'program.hash', `${programHash} (want ${PROGRAM_HASH})`);

  // The goldens, parsed here: every exact print.* row of every registered Mode.
  const goldens = [];
  let blessed = 0;
  for (const key of print.modes) {
    let rows = new Map();
    let why = '';
    try {
      rows = goldenRows(await textOf(`golden/${key}.dsp.print.txt`));
    } catch (error) {
      why = String(error.message || error);
    }
    const prints = [...rows.keys()].filter((k) => k.startsWith('print.'));
    blessed += prints.length;
    if (prints.length !== ROWS_PER_MODE)
      page.row(false, `${key} golden`, why || `${prints.length} exact print rows, not ${ROWS_PER_MODE}`);
    goldens.push(rows);
  }

  let compared = 0;
  let renderMs = 0;
  for (let mode = 0; mode < print.modes.length; mode += 1) {
    const key = print.modes[mode];
    const rows = print.sets.map((_, set) => ({ mode, set, ...STD, input: 'program' }));
    const results = await render(env, RATE, rows);
    rows.forEach((row, k) => {
      const name = (channel) => `${key} print.${print.sets[row.set]}.${channel}.hash`;
      const want = { l: goldens[mode].get(name('l').slice(key.length + 1)),
                     r: goldens[mode].get(name('r').slice(key.length + 1)) };
      compared += hashRows(page, env, RATE, row, results[k], name, want, 'golden');
      renderMs += results[k].laps.reduce((a, b) => a + b, 0);
    });
  }
  page.row(compared === ROWS_PER_MODE * print.modes.length && compared === blessed && compared > 0, 'rows.count',
           `${compared} rows compared, ${blessed} blessed print rows in ${print.modes.length} golden files`);
  page.note(`web.live.print latency: ${print.latency(STD.quality, STD.budget, RATE)} samples at STD and ${RATE} Hz `
            + "(fcdsp's figure for the setup, which a row's engine must report)");
  const seconds = compared / 2 * print.frames / RATE;
  page.note(`web.live.print time: ${compared} rows in ${((performance.now() - started) / 1000).toFixed(1)} s, `
            + `${env.contexts} contexts; the renders ${(renderMs / 1000).toFixed(1)} s for ${seconds.toFixed(0)} s of `
            + `audio (${(seconds * 1000 / renderMs).toFixed(0)}x real time)`);

  // Another render quantum, where the browser gives one.
  let quantum;
  try {
    quantum = new OfflineAudioContext({ numberOfChannels: 2, length: HINT, sampleRate: RATE, renderSizeHint: HINT })
      .renderQuantumSize;
  } catch (error) {
    quantum = String(error);
  }
  if (typeof quantum !== 'number' || quantum === QUANTUM) {
    page.note(`web.live.print quantum: this browser renders in quanta of ${QUANTUM} frames only (renderSizeHint `
              + `${HINT} gives renderQuantumSize ${quantum}): the worklet's other path is not run here`);
  } else {
    const rows = print.modes.map((_, mode) => ({ mode, set: 0, ...STD, input: 'program' }));
    const results = await render(env, RATE, rows, { hint: HINT, quantum });
    rows.forEach((row, k) => {
      const key = print.modes[row.mode];
      const want = { l: goldens[row.mode].get('print.default.l.hash'),
                     r: goldens[row.mode].get('print.default.r.hash') };
      hashRows(page, env, RATE, row, results[k], (channel) => `${key} quantum.${channel}.hash`, want, 'golden');
    });
    const z = results[0].context.after;
    page.note(`web.live.print quantum: renderSizeHint ${HINT} gives quanta of ${quantum} frames; the worklet saw `
              + `${z.quanta} quanta of ${z.lastFrames} frames in the last context, ${z.oddQuanta} of them not of `
              + `${QUANTUM}`);
  }
  page.note(`web.live.print instances: ${env.instances} engine instances in this page load`);
});
