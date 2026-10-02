// web/live/fcmp-tail.js: what the denormal range does in a browser, and the engine's load there (ADR-93, the web
// lead phase). The page of fcmp-tail.html; fcmp-live.js has the method and the protocol.
//
// A wasm module cannot flush a denormal. The engine reads a denormal input as zero and its silence gate resets it
// once its tail has run out, but between a signal and the gate its state decays through the denormal range, and a
// floor just above that range keeps it there. Two things can then differ from node: the time (a processor that
// computes denormals slowly) and the values (a browser that sets flush-to-zero on its audio thread). Neither shows
// on a print row, which is never silent. What this page records, per browser:
//
//   NOTE web.live.fpmode ...             flush-to-zero and denormals-are-zero, on or off, on the main thread and in a
//                                        worklet's process(), message handler and constructor, in an offline
//                                        context and in a live one (fcmp-fpmode-worklet.js). A live context runs
//                                        only with a gesture or Chrome's autoplay flag, else its process() did not
//                                        run (it is still suspended after 0.5 s); one that runs is waited for until
//                                        its first process() says so, at most 5 s: "did not run within 5 s" on a
//                                        running context is a device that never rendered, not a slow start
//   <key> main.cost                      a row, as node's web.engine.tail judges it: the shipped engine on this
//                                        thread with the gate off and the editor attached, 2 s of the print program
//                                        and 10 s of zeros; no two adjacent windows of the silence may cost twice
//                                        the signal's time a quantum (each window the best of three runs). A window is
//                                        node's 1/3 s, or longer where this browser's clock is too coarse for it
//   <key> worklet.ran                    a row: the four renders below were armed as asked, twice the same
//   <key> tail.values, floor.values      through the shipped worklet at the plugin's default setup: 2 s of the
//                                        program and then no source (tail), or then a floor of +-2^-120 (floor),
//                                        against the shipped engine under node (../expect/fcmp-tail.json). PASS where
//                                        equal; a NOTE with both sides where not: a browser that flushes on its audio
//                                        thread is a fact to record, not a defect
//   NOTE web.live.tail <key> worklet.cost   the time of each half of those two renders: silence against signal
//                                        through the worklet, the gate as shipped
//   NOTE web.live.load <key>             the real-time factor of 4 s of the program at STD and at HQ
//   expect.engine, expect.print, expect.shape   the expectation is this build's, for the test module's Modes
//   modes.count                          every Mode ran: the Modes the test module registers (one at least), the
//                                        expectation's Modes and rows, the value rows judged here and the rows the
//                                        worklet's own counters say it rendered all agree
//
// Pin (a query parameter, for a hand run): tick=<ms> rounds this page's clock down to that step, as a browser with a
// coarse performance.now() would.
import { Page, QUANTUM, ask, environment, expectation, ranAsAsked, render, sleep, within } from './fcmp-live.js';
import { fpMode } from './fcmp-fpmode-worklet.js';

const FORMAT = 1;                           // Tools/web/live/expect.mjs FORMAT
const RATE = 48000;
const STD = { quality: 1, budget: 0, look: -1 };
const HQ = { quality: 2, budget: 0, look: -1 };
const COST_LIMIT = 2;                       // web.engine.tail's, never loosened
const SIGNAL_QUANTA = 750;                  // 2 s
const SILENT_QUANTA = 3750;                 // 10 s
const NODE_WINDOW = 125;                    // quanta: 1/3 s, web.engine.tail's window
const RUNS = 3;
const CLOCK_STEPS = 20;                     // a timed window is at least this many steps of the clock
const REPLY_FIXED_BYTES = 320;              // WebProtocol.h kReplyFixedBytes
const WORKLET_ROWS = 4;                     // a Mode's rows through the worklet: program at STD and HQ, floor, tail
const FP_MS = 15000;                        // one context's floating-point environment: then it is not measured
const LIVE_RUN_MS = 5000;                   // a running live context's first process(): then it did not run
const LIVE_IDLE_MS = 500;                   // a live context not running by then has no gesture and no autoplay flag

const page = new Page('web.live.tail');

// ---- the floating-point environment ---------------------------------------------------------------------------------
const fpText = (mode) => (mode < 0 ? 'did not run'
  : `flush-to-zero ${mode & 1 ? 'ON' : 'OFF'}, denormals-are-zero ${mode & 2 ? 'ON' : 'OFF'}`);

async function fpNotes() {
  page.note(`web.live.fpmode main thread: ${fpText(fpMode())}`);
  for (const kind of ['offline', 'live']) {
    let context = null;
    const measure = async () => {
      context = kind === 'offline'
        ? new OfflineAudioContext({ numberOfChannels: 2, length: RATE / 10, sampleRate: RATE }) : new AudioContext();
      await context.audioWorklet.addModule('fcmp-fpmode-worklet.js');
      const node = new AudioWorkletNode(context, 'fcmp-fpmode', { numberOfInputs: 0, numberOfOutputs: 1,
                                                                 outputChannelCount: [2] });
      const made = performance.now();
      let ranAt = -1;                                   // ms from the node's making to its first process()'s message
      const processed = new Promise((ran) => node.port.addEventListener('message', (event) => {
        if (!event.data || event.data.fcmp !== 'process') return;
        ranAt = performance.now() - made;
        ran();
      }));
      node.port.start();
      node.connect(context.destination);
      if (kind === 'offline') {
        await context.startRendering();
      } else {
        context.resume().catch(() => {});                // without a gesture it stays suspended
        // Until process() says it has run: at most LIVE_RUN_MS, or LIVE_IDLE_MS while the context is not running.
        const waiting = () => {
          const waited = performance.now() - made;
          return ranAt < 0 && waited < LIVE_RUN_MS && (waited < LIVE_IDLE_MS || context.state === 'running');
        };
        while (waiting()) await Promise.race([processed, sleep(20)]);
      }
      return { ...await ask(node, 'fpmode'), ranAt, state: context.state };
    };
    try {
      // A NOTE, so it may not hold the page up: a context that never answers (a machine with no audio device) is
      // "not measured", not a stalled page.
      const got = await within(FP_MS, `the ${kind} context`, measure());
      const late = kind === 'live' && got.inProcess < 0 && got.state === 'running';
      page.note(`web.live.fpmode ${kind} context: process() `
                + `${late ? `did not run within ${LIVE_RUN_MS / 1000} s` : fpText(got.inProcess)}; message handler `
                + `${fpText(got.inHandler)}; constructor ${fpText(got.inConstructor)}`
                + (kind !== 'live' ? '' : ` (the context is ${got.state}${got.ranAt < 0 ? ''
                   : `; process() first ran ${Math.round(got.ranAt)} ms after the node was made`})`));
    } catch (error) {
      page.note(`web.live.fpmode ${kind} context: not measured (${error.message || error})`);
    } finally {
      if (kind === 'live' && context !== null) context.close().catch(() => {});
    }
  }
}

// ---- the main thread: silence against signal, the gate off ------------------------------------------------------
async function mainCost(env) {
  const { print } = env;
  const pin = Number(new URLSearchParams(location.search).get('tick')) || 0;
  const now = pin > 0 ? () => Math.floor(performance.now() / pin) * pin : () => performance.now();
  // The clock's step: the smallest change it shows in 30 ms.
  let tick = Infinity;
  for (let last = now(), until = performance.now() + 30; performance.now() < until;) {
    const t = now();
    if (t !== last) tick = Math.min(tick, t - last);
    last = t;
  }
  if (!(tick < 30)) tick = 30;
  page.note(`web.live.tail clock: performance.now() steps by ${tick.toFixed(3)} ms here; a timed window is about `
            + `${CLOCK_STEPS} steps or more, and at least ${NODE_WINDOW} quanta`);

  const x = (await WebAssembly.instantiate(env.engine, {})).instance.exports;
  x._initialize();
  const f32 = new Float32Array(x.memory.buffer);
  const u8 = new Uint8Array(x.memory.buffer);
  const scratch = x.malloc(4 * QUANTUM * 4 + 256);
  if (scratch === 0) throw new Error('the engine module is out of memory');
  const [pInL, pInR, pOutL, pOutR] = [0, 1, 2, 3].map((i) => scratch + i * QUANTUM * 4);
  const pMessage = scratch + 4 * QUANTUM * 4;
  const programQuanta = print.frames / QUANTUM;
  // One run: the program (from its start, again where it ends), then zeros, timed per window of `span` quanta.
  const run = (mode, span, activeWindows, silentWindows) => {
    const engine = x.fcmp_web_create();
    const post = (bytes) => {
      u8.set(bytes, pMessage);
      return x.fcmp_web_post(engine, pMessage, bytes.length);
    };
    const set = post(new Uint8Array(print.record(mode, 0, STD.quality, STD.budget, STD.look))) === 0
             && x.fcmp_web_configure(engine, RATE, QUANTUM) >= 0;
    x.fcmp_web_set_gate(engine, 0);
    post(print.attach(true));                           // as in the browser while the editor is open
    const active = [];
    const silent = [];
    for (let w = 0; w < activeWindows + silentWindows; w += 1) {
      if (w === activeWindows) f32.fill(0, pInL / 4, pInL / 4 + 2 * QUANTUM);
      const from = now();
      for (let q = 0; q < span; q += 1) {
        if (w < activeWindows) {
          const at = ((w * span + q) % programQuanta) * QUANTUM;
          f32.set(print.programL.subarray(at, at + QUANTUM), pInL / 4);
          f32.set(print.programR.subarray(at, at + QUANTUM), pInR / 4);
        }
        x.fcmp_web_process(engine, pInL, pInR, pOutL, pOutR, QUANTUM);
      }
      (w < activeWindows ? active : silent).push((now() - from) / span);
    }
    // The gate was off and the engine ran every block (web.engine.tail's tail.gate_off): else silence is cheap for
    // the wrong reason.
    const pReply = x.fcmp_web_reply(engine);
    const blocks = (activeWindows + silentWindows) * span;
    const running = set && post(print.pull()) >= REPLY_FIXED_BYTES
                 && print.running(u8.subarray(pReply, pReply + REPLY_FIXED_BYTES), blocks);
    x.fcmp_web_destroy(engine);
    active.sort((a, b) => a - b);
    return { active: active[(activeWindows - 1) >> 1], fastest: active[0], silent, running };
  };

  const us = (ms) => (ms * 1000).toFixed(1);
  for (let mode = 0; mode < print.modes.length; mode += 1) {
    // Untimed first (the Mode's code is compiled while it runs); its fastest window of signal also says how long a
    // window must be for this clock.
    const warm = run(mode, NODE_WINDOW, SIGNAL_QUANTA / NODE_WINDOW, 3);
    const perQuantum = warm.fastest > 0 ? warm.fastest : tick / NODE_WINDOW;
    const span = Math.min(SILENT_QUANTA,
                            Math.max(1, Math.ceil(CLOCK_STEPS * tick / perQuantum / NODE_WINDOW)) * NODE_WINDOW);
    const activeWindows = Math.max(3, Math.ceil(SIGNAL_QUANTA / span));
    const silentWindows = Math.max(4, Math.ceil(SILENT_QUANTA / span));
    let best = null;
    let running = warm.running;
    for (let i = 0; i < RUNS; i += 1) {
      await sleep(0);                                   // the title and the log stay readable between runs
      const t = run(mode, span, activeWindows, silentWindows);
      running = running && t.running;
      best = best === null ? t : { active: Math.min(best.active, t.active),
                                   silent: best.silent.map((s, w) => Math.min(s, t.silent[w])) };
    }
    let worst = 0;
    let worstAt = 0;
    for (let w = 0; w + 1 < silentWindows; w += 1) {
      const pair = Math.min(best.silent[w], best.silent[w + 1]);          // both of them slow in every run
      if (pair > worst) [worst, worstAt] = [pair, w];
    }
    const seconds = (quanta) => (quanta * QUANTUM / RATE).toFixed(1);
    page.row(running && best.active > 0 && worst < COST_LIMIT * best.active, `${print.modes[mode]} main.cost`,
             `signal ${us(best.active)} us a quantum, silence at worst ${us(worst)} us over ${seconds(2 * span)} s `
             + `from ${seconds(worstAt * span)} s (x${(worst / best.active).toFixed(2)}, limit x${COST_LIMIT}; gate `
             + `off, best of ${RUNS}, windows of ${span} quanta)`
             + (running ? '' : '; THE ENGINE DID NOT RUN EVERY BLOCK with the gate off'));
  }
}

// ---- through the shipped worklet: the values, silence against signal, the load -----------------------------------
async function workletRows(env, expect) {
  const { print } = env;
  const seconds = print.frames / RATE;
  const half = seconds / 2;
  const equal = { tail: [], floor: [] };
  const worst = { STD: null, HQ: null };
  const ended = new Set();
  const counted = { judged: 0, rendered: 0 };       // for modes.count: value rows judged, rows the worklet rendered
  for (let mode = 0; mode < print.modes.length; mode += 1) {
    const key = print.modes[mode];
    const rows = [{ mode, set: 0, ...STD, input: 'program' },
                  { mode, set: 0, ...HQ, input: 'program' },
                  { mode, set: 0, ...STD, input: 'floor', mark: true },
                  { mode, set: 0, ...STD, input: 'tail', mark: true }];
    // Twice where the rows share a context: the times are the best of two, and the values must not move.
    const renders = [];
    for (let i = 0; i < (env.suspends ? 2 : 1); i += 1) renders.push(await render(env, RATE, rows));
    const faults = [];
    renders.forEach((results, i) => rows.forEach((row, k) => {
      const asked = ranAsAsked(env, RATE, row, results[k]);
      if (!asked.ok) faults.push(`render ${i + 1}, row ${k + 1}: ${asked.text}`);
    }));
    const hashes = renders.map((results) => results.map((r) => `${print.hash(r.l)} ${print.hash(r.r)}`));
    if (hashes.some((h) => h.join() !== hashes[0].join())) faults.push('two renders of the same rows differ');
    page.row(faults.length === 0, `${key} worklet.ran`, faults[0] || `${rows.length} rows armed as asked in each of `
             + `${renders.length} render(s)${renders.length > 1 ? ', which agree' : ''}`);
    for (const context of new Set(renders[0].map((result) => result.context)))   // its counters, once a context
      counted.rendered += context.after.quanta / Math.ceil(print.frames / context.quantum);
    ended.add(renders[0][3].context.after.inChannels);

    for (const [kind, k] of [['tail', 3], ['floor', 2]]) {
      const got = renders[0][k];
      const want = expect.rows[`${key} ${kind}`];
      if (hashes[0][k] === `${want.l} ${want.r}`) {
        equal[kind].push(key);
        page.row(true, `${key} ${kind}.values`, `${hashes[0][k]}: equal to the engine under node`);
        counted.judged += 1;
      } else {
        const [lastL, denormalL] = print.tail(got.l);
        const [lastR, denormalR] = print.tail(got.r);
        page.note(`web.live.tail ${key} ${kind}.values: ${hashes[0][k]} here, ${want.l} ${want.r} under node; the `
                  + `last sample that is not a zero ${lastL}/${lastR} here, ${want.last.join('/')} under node; `
                  + `denormal samples ${denormalL}/${denormalR} here, ${want.denormal.join('/')} under node`);
        counted.judged += 1;
      }
    }

    const best = rows.map((_, k) => renders[0][k].laps.map((_, i) => Math.min(...renders.map((r) => r[k].laps[i]))));
    const total = (k) => best[k].reduce((a, b) => a + b, 0);
    const times = (a, b) => `x${(a / b).toFixed(2)}`;
    page.note(best[2].length === 2 && best[3].length === 2
      ? `web.live.tail ${key} worklet.cost: ${half} s of signal ${best[2][0].toFixed(1)} ms, then ${half} s of the `
        + `2^-120 floor ${times(best[2][1], best[2][0])} of it; ${half} s of signal ${best[3][0].toFixed(1)} ms, then `
        + `${half} s of no source ${times(best[3][1], best[3][0])} of it (best of ${renders.length})`
      : `web.live.tail ${key} worklet.cost: ${seconds} s of signal ${total(0).toFixed(1)} ms; ${half} s of signal and `
        + `${half} s of the 2^-120 floor ${times(total(2), total(0))} of it; ${half} s of signal and ${half} s of no `
        + `source ${times(total(3), total(0))} of it (whole renders: one context per row)`);
    const factor = { STD: seconds * 1000 / total(0), HQ: seconds * 1000 / total(1) };
    page.note(`web.live.load ${key}: STD ${factor.STD.toFixed(1)}x real time, HQ ${factor.HQ.toFixed(1)}x `
              + `(${seconds} s of the print program through the worklet, best of ${renders.length})`);
    for (const q of ['STD', 'HQ'])
      if (worst[q] === null || factor[q] < worst[q].factor) worst[q] = { key, factor: factor[q] };
  }
  const modes = print.modes.length;
  const not = (kind) => (equal[kind].length === modes ? ''
    : ` (not: ${print.modes.filter((key) => !equal[kind].includes(key)).join(' ')})`);
  page.note(`web.live.tail values: equal to the engine under node in ${equal.tail.length} of ${modes} Modes after the `
            + `source ends${not('tail')}, in ${equal.floor.length} of ${modes} on the 2^-120 floor${not('floor')}`);
  page.note(`web.live.tail source: an ended source reaches the worklet as ${[...ended].join(' or ')} channel(s)`);
  if (worst.STD !== null)
    page.note(`web.live.load worst: STD ${worst.STD.key} ${worst.STD.factor.toFixed(1)}x real time, HQ ${worst.HQ.key} `
              + `${worst.HQ.factor.toFixed(1)}x`);
  return counted;
}

page.run(async () => {
  const env = await environment(page);
  const { print } = env;
  const started = performance.now();

  const expect = await expectation(page, env, 'fcmp-tail.json', FORMAT);
  if (expect === null) return;
  const keys = print.modes.flatMap((key) => [`${key} tail`, `${key} floor`]);
  if (!page.row(JSON.stringify(expect.modes) === JSON.stringify(print.modes) && expect.frames === print.frames
                && JSON.stringify(Object.keys(expect.rows || {})) === JSON.stringify(keys), 'expect.shape',
                `Modes ${(expect.modes || []).join(' ')}; ${Object.keys(expect.rows || {}).length} rows (want `
                + `${keys.length}: a tail and a floor a Mode); ${expect.frames} frames a row`)) return;

  await fpNotes();
  const t1 = performance.now();
  await mainCost(env);
  const t2 = performance.now();
  const counted = await workletRows(env, expect);
  const registered = print.modes.length;               // fcmp_print_modes(): the test module's registry
  const expected = Object.keys(expect.rows).length;
  page.row(registered > 0 && expect.modes.length === registered && expected === 2 * registered
           && counted.judged === expected && counted.rendered === WORKLET_ROWS * registered, 'modes.count',
           `${registered} Modes registered; the expectation has ${expect.modes.length} Modes and ${expected} rows; `
           + `${counted.judged} value rows judged (want ${expected}); the worklet rendered ${counted.rendered} rows `
           + `(want ${WORKLET_ROWS} a Mode)`);
  const t3 = performance.now();
  const s = (ms) => (ms / 1000).toFixed(1);
  page.note(`web.live.tail time: ${s(t3 - started)} s: the main thread ${s(t2 - t1)} s, the worklet ${s(t3 - t2)} s in `
            + `${env.contexts} contexts`);
  page.note(`web.live.tail instances: ${env.instances + 1} engine instances in this page load`);
});
