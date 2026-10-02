// web/tests/print.mjs: the node twin of the browser gate's print page, and the gate's expectation tool (ADR-93, the
// web lead phase).
//
// FCMP_WEB_TEST name=web.worklet.print timeout=180 on=web args={source},{build}
//
//   node print.mjs <repository> <build directory> [--via plain] [--contexts row]
//
// Scripts/web-live.sh renders dsp.print's blessed rows through the shipped worklet in a real browser
// (web/live/fcmp-print.js) and holds the rows that have no golden to this build's engine under node
// (web/live/fcmp-extra.js and fcmp-tail.js against Tools/web/live/expect.mjs). That gate needs a browser, so it is
// not part of `verify`; this is its method without one, so that neither the method nor the tool can rot unseen.
//
// What runs is what the gate serves: <build>/site/fcmp-worklet.js as it is, in a stand-in for the
// AudioWorkletGlobalScope (as web/tests/worklet.mjs makes it: a real MessageChannel as the processor's port), over
// <build>/site/fcmp-engine.wasm; the material, the records and the hash from <build>/live/fcmp-print.wasm
// (Tools/web/live/PrintModule.h); the goldens as the page reads them, <build>/live/golden/<key>.dsp.print.txt. A
// row's engine is made as the page makes it (web/live/fcmp-live.js, "A fresh engine per row"): two records, the
// first at another QUALITY, then the worklet's counters, then the row's 1,500 quanta; a Mode's four rows follow each
// other in one processor, as they do in one context of the page. The golden files are parsed and each row is judged
// by the pages' own functions (goldenRows and ranAsAsked, imported from web/live/fcmp-live.js, which touches the
// document only inside its Page), so those run in the gate too.
//
//   golden.copies                    <build>/live/golden holds one file per registered Mode, each the blessed file
//   registers, program.hash          the shipped script registers the processor; the program is the goldens' own
//   <key> print.<set>.<l|r>.hash     the 112 blessed rows: armed at the row's first quantum with both records in,
//                                    none refused, at the setup's latency, and the hash
//   rows.count                       8 rows a Mode and no golden row left over
//   expect.ran                       Tools/web/live/expect.mjs on this build exits 0 and writes its two files (it
//                                    runs beside the rows above)
//   expect.extra, expect.tail        each file is well-formed: its format, this build's two modules by their
//                                    SHA-256, the Modes, every row and nothing else, hashes of 16 hex digits, the
//                                    latency fcdsp gives each setup, where each tail ends
//   expect.control                   the tool's rows at the plugin's default setup are the goldens' print.default
//                                    rows: its way of driving the engine is web.engine.print's
//   expect.method                    for the first and the last Mode, every row of both files through the worklet
//                                    as the pages render it equals the tool's value: under node the two ways agree,
//                                    so a difference in a browser is the browser's
//   expect.refuses                   a site with no engine: exit 2 and a reason
//
// --via plain posts one snapped record per row, the trap of the scout's report: 110 of the 112 rows then fail (with
// --contexts row, a processor per row as a browser without OfflineAudioContext.suspend needs it, 108). A
// demonstration that the rows can fail; the test never runs with it.
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage or stopped by a signal.
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, readdirSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { MessageChannel } from 'node:worker_threads';

const TEST = 'web.worklet.print';
const PROGRAM_HASH = '2e3621bff17e9df5 9960d5f0d72e0b18';     // web/live/fcmp-print.js PROGRAM_HASH
const ROWS_PER_MODE = 8;
const HEX16 = /^[0-9a-f]{16}$/;

let passed = 0;
let failed = 0;
function row(ok, name, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}     ${TEST} ${name}${detail ? ': ' + detail : ''}`);
  if (ok) passed += 1;
  else failed += 1;
  return ok;
}
function note(text) {
  console.log(`NOTE     ${text}`);
}
function finish() {
  console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
}

const [source, build, ...flags] = process.argv.slice(2);
const flag = (name, value) => {
  const i = flags.indexOf(name);
  return i >= 0 && flags[i + 1] === value;
};
if (!source || !build || flags.length % 2 !== 0
    || flags.some((f, i) => i % 2 === 0 && !flag(f, f === '--via' ? 'plain' : f === '--contexts' ? 'row' : null))) {
  console.error('usage: node print.mjs <repository> <build directory> [--via plain] [--contexts row]');
  process.exit(2);
}
const plain = flag('--via', 'plain');
const perRow = flag('--contexts', 'row');
const site = join(build, 'site');
const live = join(build, 'live');
const tool = join(source, 'Tools', 'web', 'live', 'expect.mjs');
const pages = join(source, 'web', 'live', 'fcmp-live.js');
const need = [join(site, 'fcmp-worklet.js'), join(site, 'fcmp-engine.wasm'), join(live, 'fcmp-print.wasm'),
              join(live, 'golden'), tool, pages];
if (!need.every((path) => existsSync(path))) {
  row(false, 'inputs', `no ${need.filter((path) => !existsSync(path)).join(', ')}: build the web tree first (cmake `
      + '--build --preset web)');
  finish();
}

// The expectation tool runs beside the rows below (it has threads of its own) and writes into a directory of the
// build tree, one run at a time as the gate's <build>/web-live. However this test ends, the tool is stopped and the
// directory removed: at its exit (the end, a row that stops it, an error thrown) and on SIGINT, SIGTERM or SIGHUP
// (then exit 2), the tool killed and gone before its directory is. Only a SIGKILL (ctest's timeout) skips both: the
// tool then ends by itself within seconds, and the next run removes what is left before it starts.
const expectDir = join(build, 'web-worklet-print.expect');
const removeExpectDir = () => rmSync(expectDir, { recursive: true, force: true });
removeExpectDir();
let toolProcess = null;
const toolRunning = () => toolProcess !== null && toolProcess.exitCode === null && toolProcess.signalCode === null;
process.on('exit', () => {
  if (toolRunning()) toolProcess.kill('SIGKILL');
  removeExpectDir();
});
for (const signal of ['SIGINT', 'SIGTERM', 'SIGHUP']) {
  process.on(signal, () => {
    const end = () => {
      removeExpectDir();
      process.exit(2);
    };
    if (!toolRunning()) return end();
    toolProcess.once('exit', end);
    toolProcess.kill('SIGKILL');
    setTimeout(end, 2000);
  });
}
const toolStarted = performance.now();
const toolRun = new Promise((done) => {
  const child = spawn(process.execPath, [tool, '--site', site, '--live', live, '--out', expectDir],
                      { stdio: ['ignore', 'pipe', 'pipe'] });
  toolProcess = child;
  let said = '';
  child.stdout.on('data', (chunk) => { said += chunk; });
  child.stderr.on('data', (chunk) => { said += chunk; });
  child.on('error', (error) => done({ code: -1, said: String(error), ms: performance.now() - toolStarted }));
  child.on('exit', (code) => done({ code, said, ms: performance.now() - toolStarted }));
});

const { FORMAT, QUANTUM, SETUPS, STD, TAIL_KINDS, printModule } = await import(pathToFileURL(tool).href);
const { goldenRows, ranAsAsked } = await import(pathToFileURL(pages).href);
const engineFile = readFileSync(join(site, 'fcmp-engine.wasm'));
const printFile = readFileSync(join(live, 'fcmp-print.wasm'));
const wasm = engineFile.buffer.slice(engineFile.byteOffset, engineFile.byteOffset + engineFile.byteLength);
const print = printModule(printFile);
const { frames } = print;

// ---- the stand-in scope ---------------------------------------------------------------------------------------------
let nextPort = null;
const registered = {};
globalThis.AudioWorkletProcessor = class {
  constructor() {
    this.port = nextPort;
  }
};
globalThis.registerProcessor = (name, ctor) => {
  registered[name] = ctor;
};
globalThis.sampleRate = 48000;
await import(pathToFileURL(join(site, 'fcmp-worklet.js')).href);
row(typeof registered['fcmp-engine'] === 'function', 'registers', `processors: ${Object.keys(registered).join(', ')}`);
if (!registered['fcmp-engine']) finish();

// One processor at `rate` with its port's other end, and its ready message.
async function processorAt(rate) {
  const channel = new MessageChannel();
  const inbox = [];
  let waiting = null;
  channel.port2.on('message', (data) => {
    inbox.push(data);
    if (waiting) waiting();
  });
  // The next message from the processor, or null after two seconds.
  const next = () => new Promise((done) => {
    const timer = setTimeout(() => {
      waiting = null;
      done(null);
    }, 2000);
    const look = () => {
      if (inbox.length === 0) return;
      clearTimeout(timer);
      waiting = null;
      done(inbox.shift());
    };
    waiting = look;
    look();
  });
  nextPort = channel.port1;
  globalThis.sampleRate = rate;
  const processor = new registered['fcmp-engine']({ processorOptions: { wasm } });
  const ready = await next();
  if (!ready || ready.fcmp !== 'ready') throw new Error(`the processor said ${JSON.stringify(ready)}, not ready`);
  const stats = async () => {
    channel.port2.postMessage({ fcmp: 'stats' });
    for (;;) {
      const m = await next();
      if (m === null) throw new Error('the processor did not answer with its counters');
      if (m.fcmp === 'stats') return m;
    }
  };
  return { processor, ready, stats, post: (buffer) => channel.port2.postMessage(buffer),
           close: () => { channel.port1.close(); channel.port2.close(); } };
}

// A row's input, both channels: a tail is the program's first half, and then the source has ended.
const material = (input) => (input === 'floor' ? [print.floorL, print.floorR]
  : input === 'tail' ? [print.programL.subarray(0, frames / 2), print.programR.subarray(0, frames / 2)]
    : [print.programL, print.programR]);
// The records that make a row's engine (web/live/fcmp-live.js, environment().records).
const records = (r) => {
  const own = print.record(r.mode, r.set, r.quality, r.budget, r.look).buffer;
  return plain ? [own] : [print.record(r.mode, r.set, r.quality === 1 ? 2 : 1, r.budget, r.look).buffer, own];
};

// `rows` end to end through one processor at `rate`, each armed at its first frame: web/live/fcmp-live.js's
// renderContext, with process() called as a browser calls it (zeroed outputs; no input channel once a source ended).
async function renderTogether(rate, rows) {
  const p = await processorAt(rate);
  const inputs = [[new Float32Array(QUANTUM), new Float32Array(QUANTUM)]];
  const outputs = [[new Float32Array(QUANTUM), new Float32Array(QUANTUM)]];
  const ended = [[]];
  let sent = 0;
  const out = [];
  for (const r of rows) {
    for (const record of records(r)) {
      p.post(record);
      sent += 1;
    }
    const armed = { stats: await p.stats(), sent };
    const [inL, inR] = material(r.input);
    const left = new Float32Array(frames);
    const right = new Float32Array(frames);
    for (let at = 0; at < frames; at += QUANTUM) {
      const playing = at < inL.length;
      if (playing) {
        inputs[0][0].set(inL.subarray(at, at + QUANTUM));
        inputs[0][1].set(inR.subarray(at, at + QUANTUM));
      }
      outputs[0][0].fill(0);
      outputs[0][1].fill(0);
      p.processor.process(playing ? inputs : ended, outputs);
      left.set(outputs[0][0], at);
      right.set(outputs[0][1], at);
    }
    out.push({ l: left, r: right, armed, at: out.length });
  }
  const shared = { ready: p.ready, after: await p.stats(), quantum: QUANTUM, rows: rows.length, sent };
  p.close();
  return out.map((o) => ({ ...o, context: shared }));
}
async function render(rate, rows) {
  if (!perRow) return renderTogether(rate, rows);
  const out = [];
  for (const r of rows) out.push(...await renderTogether(rate, [r]));
  return out;
}

// Whether a row ran as it was asked to (fcmp-live.js): `ok`, and the figures as a text.
const asAsked = (rate, r, result) => ranAsAsked({ print }, rate, r, result);

// ---- the blessed rows -----------------------------------------------------------------------------------------------
const programHash = `${print.hash(print.programL)} ${print.hash(print.programR)}`;
row(programHash === PROGRAM_HASH, 'program.hash', `${programHash} (want ${PROGRAM_HASH})`);

const goldenDir = join(live, 'golden');
const goldens = [];
let blessed = 0;
{
  const wanted = print.modes.map((key) => `${key}.dsp.print.txt`).sort();
  const there = readdirSync(goldenDir).sort();
  const stale = [];
  for (const key of print.modes) {
    const copy = join(goldenDir, `${key}.dsp.print.txt`);
    const original = join(source, 'tests', 'golden', 'base', 'modes', key, 'dsp.print.txt');
    const text = existsSync(copy) ? readFileSync(copy, 'utf8') : '';
    if (!existsSync(original) || readFileSync(original, 'utf8') !== text) stale.push(key);
    const rows = goldenRows(text);
    blessed += [...rows.keys()].filter((k) => k.startsWith('print.')).length;
    goldens.push(rows);
  }
  row(JSON.stringify(wanted) === JSON.stringify(there) && stale.length === 0, 'golden.copies',
      `${there.length} files in ${goldenDir} for ${print.modes.length} registered Modes`
      + (stale.length ? `; not the blessed file: ${stale.join(' ')}` : ', each the blessed file'));
}

const started = performance.now();
let compared = 0;
for (let mode = 0; mode < print.modes.length; mode += 1) {
  const key = print.modes[mode];
  const rows = print.sets.map((_, set) => ({ mode, set, quality: STD.quality, budget: STD.budget, look: STD.look,
                                             input: 'program' }));
  const results = await render(STD.rate, rows);
  rows.forEach((r, k) => {
    const ran = asAsked(STD.rate, r, results[k]);
    for (const channel of ['l', 'r']) {
      const name = `print.${print.sets[r.set]}.${channel}.hash`;
      const got = print.hash(results[k][channel]);
      const want = goldens[mode].get(name);
      compared += 1;
      row(ran.ok && got === want, `${key} ${name}`,
          (got === want ? got : want === undefined ? `${got}: the golden has no such row` : `${got} golden ${want}`)
          + (ran.ok ? '' : `; ${ran.text}`));
    }
  });
}
row(compared === ROWS_PER_MODE * print.modes.length && compared === blessed && compared > 0, 'rows.count',
    `${compared} rows compared, ${blessed} blessed print rows in ${print.modes.length} golden files`);
const rowsMs = performance.now() - started;

// ---- the expectation tool -------------------------------------------------------------------------------------------
const ran = await toolRun;
const read = (name) => {
  try {
    return JSON.parse(readFileSync(join(expectDir, name), 'utf8'));
  } catch (error) {
    return null;
  }
};
const extra = read('fcmp-extra.json');
const tail = read('fcmp-tail.json');
removeExpectDir();
note(`${TEST} time: ${compared} rows in ${(rowsMs / 1000).toFixed(1)} s; the expectation tool `
     + `${(ran.ms / 1000).toFixed(1)} s beside them`);
if (row(ran.code === 0 && extra !== null && tail !== null, 'expect.ran',
        `exit ${ran.code}; ${ran.said.trim().split('\n').pop()}`)) {
  const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');
  // What both files carry: the format, this build's two modules, the Modes, the row length.
  const headed = (file) => file.format === FORMAT && !!file.engine && file.engine.sha256 === sha256(engineFile)
                        && file.engine.bytes === engineFile.length && !!file.print
                        && file.print.sha256 === sha256(printFile) && file.print.bytes === printFile.length
                        && JSON.stringify(file.modes) === JSON.stringify(print.modes) && file.frames === frames;
  const hashed = (r, setup) => !!r && HEX16.test(r.l) && HEX16.test(r.r)
                            && r.latency === print.latency(setup.quality, setup.budget, setup.rate);
  const keysAre = (rows, keys) => JSON.stringify(Object.keys(rows || {})) === JSON.stringify(keys);

  const extraKeys = print.modes.flatMap((key) => Object.keys(SETUPS).map((name) => `${key} ${name}`));
  row(headed(extra) && JSON.stringify(extra.setups) === JSON.stringify(SETUPS) && keysAre(extra.rows, extraKeys)
      && keysAre(extra.control, print.modes)
      && extraKeys.every((k) => hashed(extra.rows[k], SETUPS[k.slice(k.indexOf(' ') + 1)]))
      && print.modes.every((key) => hashed(extra.control[key], STD)), 'expect.extra',
      `format ${extra.format}, ${Object.keys(extra.rows || {}).length} rows (want ${extraKeys.length}: `
      + `${print.modes.length} Modes x ${Object.keys(SETUPS).join(', ')}), ${Object.keys(extra.control || {}).length} `
      + 'control rows');

  const tailKeys = print.modes.flatMap((key) => TAIL_KINDS.map((kind) => `${key} ${kind}`));
  const counted = (r) => [r.last, r.denormal].every((pair) => Array.isArray(pair) && pair.length === 2
                                                             && pair.every(Number.isInteger))
                      && r.last.every((i) => i >= -1 && i < frames) && r.denormal.every((n) => n >= 0 && n <= frames);
  row(headed(tail) && keysAre(tail.rows, tailKeys) && tailKeys.every((k) => hashed(tail.rows[k], STD)
      && counted(tail.rows[k])), 'expect.tail',
      `format ${tail.format}, ${Object.keys(tail.rows || {}).length} rows (want ${tailKeys.length}: `
      + `${print.modes.length} Modes x ${TAIL_KINDS.join(', ')})`);

  const moved = print.modes.filter((key, mode) => {
    const c = (extra.control || {})[key] || {};
    return c.l !== goldens[mode].get('print.default.l.hash') || c.r !== goldens[mode].get('print.default.r.hash');
  });
  row(moved.length === 0, 'expect.control', `the tool's rows at the plugin's default setup equal the goldens' `
      + `print.default rows in ${print.modes.length - moved.length} of ${print.modes.length} Modes`
      + (moved.length ? ` (not: ${moved.join(' ')})` : ''));

  // The first and the last Mode: every row of both files as the pages render it.
  const differ = [];
  let agreed = 0;
  for (const mode of [...new Set([0, print.modes.length - 1])]) {
    const key = print.modes[mode];
    const check = async (rate, wanted) => {
      const rows = wanted.map(([, r]) => r);
      const results = await render(rate, rows);
      wanted.forEach(([name], k) => {
        const want = (name.endsWith(' tail') || name.endsWith(' floor') ? tail.rows : extra.rows)[name] || {};
        const same = asAsked(rate, rows[k], results[k]).ok && print.hash(results[k].l) === want.l
                  && print.hash(results[k].r) === want.r;
        if (same) agreed += 1;
        else differ.push(name);
      });
    };
    for (const rate of [...new Set(Object.values(SETUPS).map((s) => s.rate))]) {
      await check(rate, Object.entries(SETUPS).filter(([, s]) => s.rate === rate)
        .map(([name, s]) => [`${key} ${name}`, { mode, set: 0, quality: s.quality, budget: s.budget, look: s.look,
                                                 input: 'program' }]));
    }
    const std = { mode, set: 0, quality: STD.quality, budget: STD.budget, look: STD.look };
    await check(STD.rate, [[`${key} floor`, { ...std, input: 'floor' }], [`${key} tail`, { ...std, input: 'tail' }]]);
  }
  row(differ.length === 0 && agreed > 0, 'expect.method',
      `${agreed} row(s) of ${print.modes[0]} and ${print.modes[print.modes.length - 1]} through the worklet equal the `
      + `tool's` + (differ.length ? `; not: ${differ.join(', ')}` : ''));
}
{
  const refused = spawnSync(process.execPath, [tool, '--site', live, '--live', live, '--out', expectDir],
                            { encoding: 'utf8' });
  row(refused.status === 2 && refused.stderr.startsWith('expect: ') && !existsSync(expectDir), 'expect.refuses',
      `a site with no engine: exit ${refused.status}, "${refused.stderr.trim().slice(0, 100)}"`);
}
finish();
