// web/live/fcmp-live.js: what the live pages of the browser gate share (ADR-93, the web lead phase). An ES module.
//
// A live page is served at /live/ beside the site at / and the expectation tool's output at /expect/
// (docs/sprints/web-lead.md, "The gate's contract"), so it reaches what ships as ../fcmp-worklet.js and
// ../fcmp-engine.wasm: every row here runs through the SHIPPED worklet over the SHIPPED engine in an
// OfflineAudioContext, which needs no gesture, makes no sound and renders exactly. The material, the records and the
// hash come from the test-only module beside the page (fcmp-print.wasm, Tools/web/live/PrintModule.h): a script only
// moves bytes.
//
// The protocol is the self-test's: document.title is RUNNING, then PASS or "FAIL: <the first failing row>"; the rows
// are PASS|FAIL|NOTE lines in #funkgui-log; an uncaught error or an unhandled rejection is a FAIL at once, and no
// PASS replaces it. A page that writes no line for a minute fails the same way, so a runner never waits on a page
// that hangs.
//
// A fresh engine per row. The worklet configures its engine at the host's defaults, and a snapped Params record is
// not a fresh engine: a Mode or kernel change in a snapped block still crossfades. So a row is armed with TWO
// records, the first at another QUALITY and then the row's own: the second one changes the setup, the engine
// reconfigures in the message handler (WebEngine.cpp, applyParams), and EngineHost::configure builds a new engine,
// snapped and cleared at the row's values. Then the worklet's counters are asked for before the render goes on: a
// record posted to a context that is rendering can land after the quanta it was meant for.
//
// Contexts. Where the browser has OfflineAudioContext.suspend, the rows of one group share a context: the material of
// each lies end to end in one buffer, and at each row's first frame the context is suspended, the next row is armed
// and the render resumes. A suspend time must be a whole number of quanta and exact in seconds (4 s rows at 48 kHz
// are; at 44.1 kHz they are not), so anything else, and every browser without suspend, gets one context per row.
// Chrome fails at the 125th engine instance of one page load (the contexts are not freed in time): a page counts
// its instances and stops short of that.
//
// web/tests/print.mjs imports this file under node for goldenRows() and ranAsAsked(), so that the gate without a
// browser runs them too: nothing here touches the document, the window or Web Audio outside a function.
//
// Pins (query parameters, for a hand run; the gate opens the pages without any):
//   contexts=row   one context per row even where the browser can suspend one
//   via=plain      one snapped record per row instead of the two above: the trap, kept as the demonstration that the
//                  rows can fail (the page fails)

export const QUANTUM = 128;                 // fcmp-worklet.js QUANTUM: the engine's block and the usual render quantum
const RECORD_BYTES = 140;                   // WebProtocol.h sizeof(ParamsMsg)
const REPLY_FIXED_BYTES = 320;              // WebProtocol.h kReplyFixedBytes
const STALL_MS = 60000;                     // no line for this long: the page fails
const ANSWER_MS = 10000;                    // the worklet's ready message and its counters
const RENDER_MS = 120000;                   // one offline render
const INSTANCE_LIMIT = 120;                 // engine instances per page load: Chrome throws at the 125th

export const sleep = (ms) => new Promise((done) => setTimeout(done, ms));
export const within = (ms, what, promise) => {
  let timer = 0;
  const late = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(`${what}: no answer in ${ms} ms`)), ms);
  });
  return Promise.race([promise, late]).finally(() => clearTimeout(timer));
};
const hex8 = (value) => value.toString(16).padStart(8, '0');

// ---- the verdict ----------------------------------------------------------------------------------------------------
// As web/main.js's Verdict: a failing row is named by the title when the run finishes (the rows after it are still
// logged), an uncaught error fails the run at once, and once there is a failure the title is never PASS again.
export class Page {
  constructor(test) {
    this.test = test;
    this.failure = '';
    this.finished = false;
    this.urgent = false;
    this.passed = 0;
    this.failed = 0;
    this.log = document.getElementById('funkgui-log');
    this.stall = 0;
    globalThis.fcmpLive = this;             // the page's own first handler (in its .html) stands down
    window.addEventListener('error', (event) => this.uncaught(`error: ${event.message}`));
    window.addEventListener('unhandledrejection', (event) => this.uncaught(`rejection: ${event.reason}`));
    this.show('');
  }

  title() {
    if (this.failure !== '' && (this.finished || this.urgent)) return `FAIL: ${this.failure}`;
    return this.finished ? 'PASS' : 'RUNNING';
  }

  show(line) {
    if (line) this.log.textContent += `${line}\n`;      // the line first: a runner reads the log at the verdict
    document.title = this.title();
    clearTimeout(this.stall);
    if (!this.finished) this.stall = setTimeout(() => this.uncaught(`no line within ${STALL_MS / 1000} s`), STALL_MS);
  }

  row(ok, name, detail = '') {
    if (!ok && this.failure === '') this.failure = name;
    if (ok) this.passed += 1;
    else this.failed += 1;
    this.show(`${ok ? 'PASS' : 'FAIL'}     ${this.test} ${name}${detail ? ': ' + detail : ''}`);
    return ok;
  }

  note(text) {
    this.show(`NOTE     ${text}`);
  }

  uncaught(what) {
    this.urgent = true;
    this.row(false, 'uncaught', what);
  }

  // Runs the page's rows and ends the run: whatever main() throws is a failing row.
  async run(main) {
    try {
      await main();
    } catch (error) {
      this.row(false, 'ran', String((error && error.stack) || error));
    }
    this.finished = true;
    this.show(`${this.failed === 0 ? 'PASS' : 'FAIL'}     ${this.test}: ${this.passed} row(s) passed, `
              + `${this.failed} failed`);
  }
}

// ---- files ----------------------------------------------------------------------------------------------------------
async function fetched(url) {
  const response = await fetch(url, { cache: 'no-store' });
  if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
  return response;
}
export const bytesOf = async (url) => (await fetched(url)).arrayBuffer();
export const textOf = async (url) => (await fetched(url)).text();
export const jsonOf = async (url) => (await fetched(url)).json();
export async function sha256(bytes) {
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return Array.from(digest, (b) => b.toString(16).padStart(2, '0')).join('');
}

// A golden file as the probes write it: "key<TAB>value<TAB>tolerance" lines, '#' comments. The exact rows.
export function goldenRows(text) {
  const rows = new Map();
  for (const line of text.split('\n')) {
    if (line.startsWith('#')) continue;
    const [key, value, tolerance] = line.split('\t');
    if (key && value !== undefined && String(tolerance).trim() === 'exact') rows.set(key, value);
  }
  return rows;
}

// ---- the test module ------------------------------------------------------------------------------------------------
// fcmp-print.wasm with its buffers. Its memory never grows (cmake/FcmpWeb.cmake), so the views stay valid.
export async function printModule(bytes) {
  const x = (await WebAssembly.instantiate(bytes, {})).instance.exports;
  x._initialize();
  const frames = x.fcmp_print_frames();
  const pA = x.malloc(frames * 4);
  const pB = x.malloc(frames * 4);
  const pSmall = x.malloc(1024);
  if (pA === 0 || pB === 0 || pSmall === 0) throw new Error('fcmp-print.wasm is out of memory');
  const u8 = new Uint8Array(x.memory.buffer);
  const u32 = new Uint32Array(x.memory.buffer);
  const i32 = new Int32Array(x.memory.buffer);
  const f32 = new Float32Array(x.memory.buffer);
  const text = (at) => {
    let s = '';
    for (let i = at; at !== 0 && u8[i] !== 0; i += 1) s += String.fromCharCode(u8[i]);
    return s;
  };
  const pair = (fill) => {
    fill(pA, pB);
    return [f32.slice(pA / 4, pA / 4 + frames), f32.slice(pB / 4, pB / 4 + frames)];
  };
  const [programL, programR] = pair(x.fcmp_print_program);
  const [floorL, floorR] = pair(x.fcmp_print_floor);
  const modes = [];
  for (let i = 0; i < x.fcmp_print_modes(); i += 1) modes.push(text(x.fcmp_print_key(i)));
  const sets = [0, 1, 2, 3].map((s) => text(x.fcmp_print_set(s)));
  const small = (n) => u8.slice(pSmall, pSmall + n);
  return {
    frames, modes, sets, programL, programR, floorL, floorR,
    // One Params record as an ArrayBuffer of its own: what the editor module posts.
    record(mode, set, quality, budget, look, snap = 1) {
      if (x.fcmp_print_record(mode, set, quality, budget, look, snap, pSmall) !== RECORD_BYTES)
        throw new Error(`fcmp-print.wasm has no record for Mode ${mode}, set ${set}`);
      return small(RECORD_BYTES).buffer;
    },
    attach: (attached) => small(x.fcmp_print_attach(attached ? 1 : 0, pSmall)),
    pull: () => small(x.fcmp_print_pull(pSmall)),
    // Whether a reply's fixed part says a configured engine that is not gated and has published `blocks` blocks.
    running(reply, blocks) {
      u8.set(reply.subarray(0, REPLY_FIXED_BYTES), pSmall);
      return x.fcmp_print_running(pSmall, REPLY_FIXED_BYTES, blocks) === 1;
    },
    latency: (quality, budget, rate) => x.fcmp_print_latency(quality, budget, rate),
    hash(samples) {
      f32.set(samples, pA / 4);
      x.fcmp_print_hash(pA, samples.length, pSmall);
      return hex8(u32[pSmall / 4 + 1]) + hex8(u32[pSmall / 4]);
    },
    // [the index of the last sample that is not a zero, the number of denormal samples]
    tail(samples) {
      f32.set(samples, pA / 4);
      x.fcmp_print_tail(pA, samples.length, pSmall);
      return [i32[pSmall / 4], i32[pSmall / 4 + 1]];
    },
  };
}

// ---- the shipped worklet --------------------------------------------------------------------------------------------
// The node as web/main.js makes it, and its ready message.
export const makeNode = (context, wasm) => within(ANSWER_MS, 'the audio processor', new Promise((made, bad) => {
  const node = new AudioWorkletNode(context, 'fcmp-engine', {
    numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit',
    channelInterpretation: 'speakers', processorOptions: { wasm } });
  node.onprocessorerror = () => bad(new Error('the audio processor stopped'));
  const hear = (event) => {
    const m = event.data;
    if (!m || (m.fcmp !== 'ready' && m.fcmp !== 'error')) return;
    node.port.removeEventListener('message', hear);
    if (m.fcmp === 'ready') made({ node, ready: m });
    else bad(new Error(m.error));
  };
  node.port.addEventListener('message', hear);
  node.port.start();
}));
export const ask = (node, what) => within(ANSWER_MS, `the worklet's ${what}`, new Promise((answered) => {
  const hear = (event) => {
    if (!event.data || event.data.fcmp !== what) return;
    node.port.removeEventListener('message', hear);
    answered(event.data);
  };
  node.port.addEventListener('message', hear);
  node.port.postMessage({ fcmp: what });
}));

// ---- the page's environment -----------------------------------------------------------------------------------------
// The shipped files, the test module and the pins. `page` gets a NOTE of what was loaded.
export async function environment(page) {
  const query = new URLSearchParams(location.search);
  const engine = await bytesOf('../fcmp-engine.wasm');
  const printBytes = await bytesOf('fcmp-print.wasm');
  const print = await printModule(printBytes);
  const half = print.frames / 2;
  const env = {
    engine, printBytes, print,
    worklet: '../fcmp-worklet.js',
    suspends: typeof OfflineAudioContext.prototype.suspend === 'function' && query.get('contexts') !== 'row',
    plain: query.get('via') === 'plain',
    instances: 0,
    contexts: 0,
    // A row's input, both channels. A tail is the program's first half: then the source has ended.
    material(input) {
      if (input === 'floor') return [print.floorL, print.floorR];
      if (input === 'tail') return [print.programL.subarray(0, half), print.programR.subarray(0, half)];
      return [print.programL, print.programR];
    },
    // The records that make a row's engine: see "A fresh engine per row" at the top.
    records(row) {
      const own = print.record(row.mode, row.set, row.quality, row.budget, row.look);
      if (this.plain) return [own];
      return [print.record(row.mode, row.set, row.quality === 1 ? 2 : 1, row.budget, row.look), own];
    },
  };
  page.note(`${page.test} browser: ${navigator.userAgent}`);
  page.note(`${page.test} files: ../fcmp-engine.wasm ${engine.byteLength} bytes, ../fcmp-worklet.js, fcmp-print.wasm `
            + `${printBytes.byteLength} bytes; ${print.modes.length} Modes, ${print.frames} frames a row; `
            + (env.suspends ? 'rows share a context where the suspend times are exact' : 'one context per row')
            + (env.plain ? '; via=plain: ONE record per row (the trap)' : ''));
  return env;
}

// One file of the expectation tool (Tools/web/live/expect.mjs), from /expect/, and the rows that it is this build's:
//   expect                        only when the file is not there
//   expect.engine, expect.print   it was made from the engine and the test module this page is served: an
//                                 expectation of another build says nothing
// The file, or null after a failing row.
export async function expectation(page, env, name, format) {
  let expect;
  try {
    expect = await jsonOf(`../expect/${name}`);
  } catch (error) {
    page.row(false, 'expect', `${error.message || error}: the gate runs Tools/web/live/expect.mjs first`);
    return null;
  }
  page.note(`${page.test} expectation: ${expect.by}`);
  const engineSha = await sha256(env.engine);
  const printSha = await sha256(env.printBytes);
  const engine = page.row(expect.format === format && !!expect.engine && expect.engine.sha256 === engineSha,
                          'expect.engine', `format ${expect.format} (want ${format}); made from an engine with sha256 `
                          + `${expect.engine && expect.engine.sha256}, this page is served ${engineSha}`);
  const print = page.row(!!expect.print && expect.print.sha256 === printSha, 'expect.print',
                         `made with a test module with sha256 ${expect.print && expect.print.sha256}, this page is `
                         + `served ${printSha}`);
  return engine && print ? expect : null;
}

// ---- rendering ------------------------------------------------------------------------------------------------------
// One context: `rows` end to end, each armed at its first frame. See render().
async function renderContext(env, rate, rows, options) {
  const { frames } = env.print;
  const n = rows.length;
  if (env.instances >= INSTANCE_LIMIT)
    throw new Error(`this page load has made ${env.instances} engine instances: Chrome fails at the 125th`);
  if (rows.some((row, k) => row.input === 'tail' && k + 1 < n)) throw new Error('a tail ends its context');
  const init = { numberOfChannels: 2, length: n * frames, sampleRate: rate };
  if (options.hint) init.renderSizeHint = options.hint;
  const context = new OfflineAudioContext(init);
  const quantum = context.renderQuantumSize === undefined ? QUANTUM : context.renderQuantumSize;
  await context.audioWorklet.addModule(env.worklet);
  const made = await makeNode(context, env.engine);
  env.instances += 1;
  env.contexts += 1;

  // The input at the context's rate (or the browser resamples it), played once from frame 0.
  const ended = rows[n - 1].input === 'tail' ? frames / 2 : 0;
  const buffer = context.createBuffer(2, n * frames - ended, rate);
  rows.forEach((row, k) => {
    const [left, right] = env.material(row.input);
    buffer.copyToChannel(left, 0, k * frames);
    buffer.copyToChannel(right, 1, k * frames);
  });
  const source = context.createBufferSource();
  source.buffer = buffer;
  source.connect(made.node).connect(context.destination);
  source.start();

  let sent = 0;
  const arm = async (row) => {
    for (const record of env.records(row)) {
      made.node.port.postMessage(record);
      sent += 1;
    }
    return { stats: await ask(made.node, 'stats'), sent };      // the round trip: the records are in
  };
  const out = rows.map(() => ({ armed: null, laps: [] }));
  let fault = null;
  let went = 0;                                                  // when the render last went on
  // Suspends at `frame`, which ends a lap of row k; arms the next row when the frame is its first. The render goes
  // on whatever happened here: a fault is thrown once it has ended.
  const stopAt = (frame, k, next) => {
    context.suspend(frame / rate).then(async () => {
      out[k].laps.push(performance.now() - went);
      try {
        if (next) out[k + 1].armed = await arm(rows[k + 1]);
      } catch (error) {
        fault = fault || error;
      }
      went = performance.now();
      await context.resume();
    }).catch((error) => {
      fault = fault || error;
    });
  };
  out[0].armed = await arm(rows[0]);
  for (let k = 0; k < n; k += 1) {
    if (rows[k].mark && options.marks) stopAt(k * frames + frames / 2, k, false);
    if (k + 1 < n) stopAt((k + 1) * frames, k, true);
  }
  went = performance.now();
  const rendered = await within(RENDER_MS, 'the offline render', context.startRendering());
  out[n - 1].laps.push(performance.now() - went);
  if (fault !== null) throw fault;
  const after = await ask(made.node, 'stats');
  const left = rendered.getChannelData(0);
  const right = rendered.getChannelData(1);
  const shared = { ready: made.ready, after, quantum, rows: n, sent };
  return out.map((o, k) => ({ context: shared, at: k, armed: o.armed, laps: o.laps,
                              l: left.subarray(k * frames, (k + 1) * frames),
                              r: right.subarray(k * frames, (k + 1) * frames) }));
}

// Renders `rows` through the shipped worklet at `rate`, each on a fresh engine, and returns one result per row.
//   a row      { mode, set, quality, budget, look, input, mark }: the Mode's index and the set's, the setup as plain
//              values (look -1: the set's own LOOKAHEAD), the input ('program', 'floor', or 'tail', which must be
//              the last row), and whether the time of each half is wanted
//   options    { hint, quantum }: a renderSizeHint and the render quantum it gives (the caller has asked the browser)
//   a result   { l, r, armed: { stats, sent }, laps, at, context: { ready, after, quantum, rows, sent } }: the
//              output, the worklet's counters when the row was armed and how many records the context had been sent
//              by then, the row's wall-clock time in ms (two laps, one per half, where a mark was taken), its place
//              in its context, and the context's ready message and last counters
// The rows share one context where the browser can suspend it at each row's first frame (see the top).
export async function render(env, rate, rows, options = {}) {
  const { frames } = env.print;
  const quantum = options.quantum || QUANTUM;
  const exact = (frame) => frame % quantum === 0 && (frame / rate) * rate === frame;
  const together = env.suspends && exact(frames) && exact(frames / 2);
  if (together) return renderContext(env, rate, rows, { hint: options.hint, marks: true });
  const results = [];
  for (const row of rows) results.push(...await renderContext(env, rate, [row], { hint: options.hint, marks: false }));
  return results;
}

// Whether a row ran as it was asked to: armed at its first frame with every record in and none refused, on an engine
// with the setup's latency (fcdsp's own figure, from the test module), in a context at the rate asked for that
// rendered every quantum with no refusal. `ok`, and the figures as a text.
export function ranAsAsked(env, rate, row, result) {
  const perRow = Math.ceil(env.print.frames / result.context.quantum);
  const a = result.armed === null ? {} : result.armed.stats;
  const z = result.context.after;
  const want = { quanta: result.at * perRow, records: result.armed === null ? -1 : result.armed.sent,
                 latency: env.print.latency(row.quality, row.budget, rate) };
  const ok = a.ok === true && a.quanta === want.quanta && a.records === want.records && a.refused === 0
          && a.latency === want.latency && z.ok === true && z.quanta === result.context.rows * perRow
          && z.records === result.context.sent && z.refused === 0 && z.lastFrames === result.context.quantum
          && z.oddQuanta === (result.context.quantum === QUANTUM ? 0 : z.quanta)
          && result.context.ready.sampleRate === rate;
  return { ok, text: `armed at quantum ${a.quanta} (want ${want.quanta}) with ${a.records} record(s) in (want `
                     + `${want.records}), ${a.refused} refused, latency ${a.latency} (want ${want.latency}); the `
                     + `context ended at ${z.quanta} quanta of ${z.lastFrames} frames (want `
                     + `${result.context.rows * perRow} of ${result.context.quantum}), ${z.records} record(s) in `
                     + `(want ${result.context.sent}), ${z.refused} refused, ${result.context.ready.sampleRate} Hz` };
}

// One row's two hash rows on `page`: each channel against `want` ({ l, r }, from `source`), and the row ran as asked.
// Returns the number of rows written.
export function hashRows(page, env, rate, row, result, name, want, source) {
  const ran = ranAsAsked(env, rate, row, result);
  for (const channel of ['l', 'r']) {
    const got = env.print.hash(result[channel]);
    const wanted = want ? want[channel] : undefined;
    const equal = got === wanted;
    page.row(ran.ok && equal, name(channel),
             (equal ? got : wanted === undefined ? `${got}: ${source} has no such row` : `${got} ${source} ${wanted}`)
             + (ran.ok ? '' : `; ${ran.text}`));
  }
  return 2;
}
