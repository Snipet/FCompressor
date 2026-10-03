#!/usr/bin/env node
// Tools/web/live/expect.mjs: the expectation tool of the browser gate (ADR-93, the web lead phase;
// docs/sprints/web-lead.md, "The gate's contract").
//
//   node Tools/web/live/expect.mjs --site <site> --live <live dir> --out <expect dir>
//
// What a live page compares with and node can compute: the rows that have no golden. dsp.print blesses the plugin's
// default setup only (STD, no lookahead, 48 kHz), and nothing blessed reaches the denormal range, where the wasm
// engine and the native one differ in the last bits (wasm cannot flush a denormal). So these rows are held to the
// SHIPPED engine (<site>/fcmp-engine.wasm) under this node, never to a native value: the browser's compiler against
// V8's, on the same module. The engine is driven directly through its C ABI, as web.engine.print drives it (a fresh
// engine, a snapped Params record, fcmp_web_configure, 128-frame quanta); the material, the records and the hash
// are the test module's (<live dir>/fcmp-print.wasm, Tools/web/live/PrintModule.h). The pages reach the same
// engine through the shipped worklet instead, so a row also holds the worklet's way of making a fresh engine to this
// one.
//
// It writes two files, which the gate serves at /expect/ (plain JSON; the same rows for the same two modules):
//   fcmp-extra.json   per Mode, the default set at the setups of SETUPS below: ECO, HQ, HQ with a 5 ms lookahead
//                     budget and 2 ms of lookahead, and STD and HQ at 44.1 kHz (the program's samples played at that
//                     rate). `rows["<key> <setup>"] = { l, r, latency }`: each channel's hash and the latency in
//                     samples. `control["<key>"]` is the same at the plugin's default setup: the blessed
//                     print.default rows, which web/tests/print.mjs holds this tool's method to.
//   fcmp-tail.json    per Mode at the plugin's default setup, the two materials of the denormal range:
//                     `rows["<key> tail"]`: the program's first half, then no source (the engine's tail runs out and
//                     its silence gate closes); `rows["<key> floor"]`: the first half, then a floor of +-2^-120
//                     (the gate stays open and the engine's state sits in the denormal range). Beside l, r and
//                     latency: `last` (per channel, the index of the last sample that is not a zero) and `denormal`
//                     (per channel, the number of denormal samples), so that a browser whose audio thread flushes to
//                     zero can be told from one that computes something else.
// Both carry `format`, `by`, the two modules' byte counts and SHA-256 (a page refuses an expectation made from
// another engine than the one it is served), `frames` and `modes`.
//
// Exit 0, or 2 with a reason on stderr. No dependency and no build: node 22 or later and the two wasm files. The
// Modes are shared out over at most four worker threads (this file again), which changes nothing in what is written.
// web/tests/print.mjs imports printModule from here, so nothing runs on import.
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from 'node:fs';
import { availableParallelism } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';

export const FORMAT = 1;
export const QUANTUM = 128;                 // the AudioWorklet's render quantum: frames per fcmp_web_process call
// A setup: the context's rate, and QUALITY, the lookahead budget and LOOKAHEAD as plain values (0 ECO, 1 STD, 2 HQ;
// 0 off, 1 5 ms, 2 20 ms; look -1: the set's own LOOKAHEAD, else ms).
export const STD = { rate: 48000, quality: 1, budget: 0, look: -1 };
export const SETUPS = {
  eco: { rate: 48000, quality: 0, budget: 0, look: -1 },
  hq: { rate: 48000, quality: 2, budget: 0, look: -1 },
  hqla: { rate: 48000, quality: 2, budget: 1, look: 2 },
  'std@44100': { rate: 44100, quality: 1, budget: 0, look: -1 },
  'hq@44100': { rate: 44100, quality: 2, budget: 0, look: -1 },
};
export const TAIL_KINDS = ['tail', 'floor'];

const RECORD_BYTES = 140;                   // WebProtocol.h sizeof(ParamsMsg)
const hex8 = (value) => value.toString(16).padStart(8, '0');
const instance = (bytes) => {
  const x = new WebAssembly.Instance(new WebAssembly.Module(bytes), {}).exports;
  x._initialize();
  return x;
};

// fcmp-print.wasm with its buffers (its memory never grows, so the views stay valid).
export function printModule(bytes) {
  const x = instance(bytes);
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
  return {
    frames, modes, sets, programL, programR, floorL, floorR,
    // One Params record: a Uint8Array of its own.
    record(mode, set, quality, budget, look, snap = 1) {
      if (x.fcmp_print_record(mode, set, quality, budget, look, snap, pSmall) !== RECORD_BYTES)
        throw new Error(`fcmp-print.wasm has no record for Mode ${mode}, set ${set}`);
      return u8.slice(pSmall, pSmall + RECORD_BYTES);
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

// The shipped engine, driven directly. render(): a fresh engine at `record`, configured at `rate`, `frames` frames
// in 128-frame quanta; the input has `sourceFrames` frames, and past them the engine is given no input, as the worklet
// gives it none once a source has ended.
function engineModule(bytes, frames) {
  const x = instance(bytes);
  const scratch = x.malloc(4 * QUANTUM * 4 + RECORD_BYTES);
  if (scratch === 0) throw new Error('fcmp-engine.wasm is out of memory');
  const [pInL, pInR, pOutL, pOutR] = [0, 1, 2, 3].map((i) => scratch + i * QUANTUM * 4);
  const pMessage = scratch + 4 * QUANTUM * 4;
  const u8 = new Uint8Array(x.memory.buffer);
  const f32 = new Float32Array(x.memory.buffer);
  return {
    render(record, rate, inL, inR, sourceFrames) {
      const engine = x.fcmp_web_create();
      if (engine === 0) throw new Error('fcmp_web_create failed');
      u8.set(record, pMessage);
      const posted = x.fcmp_web_post(engine, pMessage, RECORD_BYTES);
      const latency = x.fcmp_web_configure(engine, rate, QUANTUM);
      if (posted !== 0 || latency < 0) {
        x.fcmp_web_destroy(engine);
        throw new Error(`the engine refused a setup (fcmp_web_post ${posted}, fcmp_web_configure ${latency})`);
      }
      const l = new Float32Array(frames);
      const r = new Float32Array(frames);
      for (let at = 0; at < frames; at += QUANTUM) {
        const none = at >= sourceFrames;
        if (!none) {
          f32.set(inL.subarray(at, at + QUANTUM), pInL / 4);
          f32.set(inR.subarray(at, at + QUANTUM), pInR / 4);
        }
        x.fcmp_web_process(engine, none ? 0 : pInL, none ? 0 : pInR, pOutL, pOutR, QUANTUM);
        l.set(f32.subarray(pOutL / 4, pOutL / 4 + QUANTUM), at);
        r.set(f32.subarray(pOutR / 4, pOutR / 4 + QUANTUM), at);
      }
      x.fcmp_web_destroy(engine);
      return { l, r, latency };
    },
  };
}

// The rows of the Modes `modes` (indices): what one thread computes. { control, extra, tail }, keyed as in the files.
function rowsOf(engineBytes, printBytes, modes) {
  const print = printModule(printBytes);
  const { frames } = print;
  const engine = engineModule(engineBytes, frames);
  // One row: the default set of `mode` at `setup`, the input's first `sourceFrames` frames and then no source.
  const row = (mode, setup, inL, inR, sourceFrames) => {
    const record = print.record(mode, 0, setup.quality, setup.budget, setup.look);
    const out = engine.render(record, setup.rate, inL, inR, sourceFrames);
    const want = print.latency(setup.quality, setup.budget, setup.rate);
    if (out.latency !== want)
      throw new Error(`${print.modes[mode]}: the engine's latency is ${out.latency} samples where fcdsp gives the `
                      + `setup ${want}`);
    return out;
  };
  const hashes = (out) => ({ l: print.hash(out.l), r: print.hash(out.r), latency: out.latency });
  const rows = { control: {}, extra: {}, tail: {} };
  for (const mode of modes) {
    const key = print.modes[mode];
    rows.control[key] = hashes(row(mode, STD, print.programL, print.programR, frames));
    for (const [name, setup] of Object.entries(SETUPS))
      rows.extra[`${key} ${name}`] = hashes(row(mode, setup, print.programL, print.programR, frames));
    for (const kind of TAIL_KINDS) {
      const out = kind === 'tail' ? row(mode, STD, print.programL, print.programR, frames / 2)
                                  : row(mode, STD, print.floorL, print.floorR, frames);
      const [lastL, denormalL] = print.tail(out.l);
      const [lastR, denormalR] = print.tail(out.r);
      rows.tail[`${key} ${kind}`] = { ...hashes(out), last: [lastL, lastR], denormal: [denormalL, denormalR] };
    }
  }
  return rows;
}

// The two files' contents, from the bytes of the shipped engine and of the test module. The Modes are shared out
// over `threads` worker threads (this file again, with a share as its workerData); what comes out does not depend on
// how many there are.
export async function expectations(engineBytes, printBytes, threads = Math.min(4, availableParallelism())) {
  const { frames, modes } = printModule(printBytes);
  if (modes.length === 0 || frames % (2 * QUANTUM) !== 0)
    throw new Error(`fcmp-print.wasm gives ${modes.length} Modes and ${frames} frames a row`);
  const shares = Array.from({ length: Math.max(1, Math.min(threads, modes.length)) }, () => []);
  modes.forEach((_, mode) => shares[mode % shares.length].push(mode));
  const parts = await Promise.all(shares.map((share) => new Promise((done, failed) => {
    const worker = new Worker(new URL(import.meta.url),
                              { workerData: { fcmpExpect: { engineBytes, printBytes, modes: share } } });
    worker.once('message', done);
    worker.once('error', failed);
    worker.once('exit', (code) => failed(new Error(`a worker thread ended with code ${code} and no rows`)));
  })));
  const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex');
  const head = {
    format: FORMAT,
    by: `Tools/web/live/expect.mjs: the shipped engine under node ${process.version}, driven directly `
      + `(a fresh engine, a snapped Params record, fcmp_web_configure, ${QUANTUM}-frame quanta)`,
    engine: { bytes: engineBytes.length, sha256: sha256(engineBytes) },
    print: { bytes: printBytes.length, sha256: sha256(printBytes) },
    frames,
    modes,
  };
  const extra = { ...head, setups: SETUPS, rows: {}, control: {} };
  const tail = { ...head, setup: STD, rows: {} };
  modes.forEach((key, mode) => {                        // in Mode order, whichever thread made a row
    const part = parts[mode % shares.length];
    extra.control[key] = part.control[key];
    for (const name of Object.keys(SETUPS)) extra.rows[`${key} ${name}`] = part.extra[`${key} ${name}`];
    for (const kind of TAIL_KINDS) tail.rows[`${key} ${kind}`] = part.tail[`${key} ${kind}`];
  });
  return { extra, tail };
}

async function main(argv) {
  const usage = 'usage: node expect.mjs --site <site> --live <live dir> --out <expect dir>';
  const opt = {};
  for (let i = 0; i < argv.length; i += 2) {
    if (!['--site', '--live', '--out'].includes(argv[i]) || argv[i + 1] === undefined) throw new Error(usage);
    opt[argv[i].slice(2)] = resolve(argv[i + 1]);
  }
  if (!opt.site || !opt.live || !opt.out) throw new Error(usage);
  const enginePath = join(opt.site, 'fcmp-engine.wasm');
  const printPath = join(opt.live, 'fcmp-print.wasm');
  for (const path of [enginePath, printPath])
    if (!existsSync(path)) throw new Error(`there is no ${path}: build the web tree (cmake --build --preset web)`);
  const started = performance.now();
  const made = await expectations(readFileSync(enginePath), readFileSync(printPath));
  mkdirSync(opt.out, { recursive: true });
  for (const [name, content] of [['fcmp-extra.json', made.extra], ['fcmp-tail.json', made.tail]])
    writeFileSync(join(opt.out, name), `${JSON.stringify(content, null, 1)}\n`);
  const modes = made.extra.modes.length;
  console.log(`expect: fcmp-extra.json: ${Object.keys(made.extra.rows).length} rows (${modes} Modes x `
              + `${Object.keys(SETUPS).join(', ')}) and ${Object.keys(made.extra.control).length} control rows`);
  console.log(`expect: fcmp-tail.json: ${Object.keys(made.tail.rows).length} rows (${modes} Modes x `
              + `${TAIL_KINDS.join(', ')})`);
  console.log(`expect: ${opt.out}: the engine of ${made.extra.engine.bytes} bytes (sha256 `
              + `${made.extra.engine.sha256.slice(0, 16)}) under node ${process.version} in `
              + `${((performance.now() - started) / 1000).toFixed(1)} s`);
}

if (!isMainThread) {
  // A worker of expectations(); nothing when another script's worker imports this file.
  const share = workerData && workerData.fcmpExpect;
  if (share) parentPort.postMessage(rowsOf(share.engineBytes, share.printBytes, share.modes));
} else if (process.argv[1] && pathToFileURL(realpathSync(process.argv[1])).href === import.meta.url) {
  main(process.argv.slice(2)).catch((error) => {
    console.error(`expect: ${(error && error.message) || error}`);
    process.exit(2);
  });
}
