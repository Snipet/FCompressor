// web/tests/worklet.mjs: the shipped worklet script over the shipped engine, under node (ADR-93, web Sprint D).
//
// FCMP_WEB_TEST name=web.worklet timeout=120 args={source}/web/fcmp-worklet.js,{engine},{source}/web/loop.js
//
//   node worklet.mjs <fcmp-worklet.js> <fcmp-engine.wasm> <loop.js>
//
// web/fcmp-worklet.js is imported as it is (web/package.json: an ES module) into a stand-in for the
// AudioWorkletGlobalScope: AudioWorkletProcessor, registerProcessor and sampleRate, and a real MessageChannel as the
// processor's port, so a buffer is transferred as a browser transfers it. The rows:
//   1. the constructor: ready with the ABI version, the latency and the rate, the bytes kept, nothing else said; a
//      module that cannot be compiled is an error message and silence, never a throw;
//   2. the self-check: only on request, the engine's constant, and refused while a source plays;
//   3. the wire rule: a Pull's reply comes back in its carrier (the record's length is Header::bytes, not the
//      buffer's), other records have no reply, a refusal is counted and nothing is sent, the inbox's old bytes are
//      never posted, anything else on the port is ignored;
//   4. process(): bit-equal to the module driven directly, for any frame count and every shape of input and output;
//      it posts nothing, calls no console function, survives an engine that traps, and ALLOCATES NOTHING: V8's
//      sampling heap profiler at an interval of one byte attributes every allocation to a call stack, and no byte may
//      be allocated under a frame of fcmp-worklet.js over 20,000 quanta of every path.
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { readFileSync } from 'node:fs';
import inspector from 'node:inspector';
import { pathToFileURL } from 'node:url';
import { MessageChannel } from 'node:worker_threads';

const TEST = 'web.worklet';
const SELF_CHECK_HASH = '5a96ce217d29ca6f';       // Tools/web/enginecheck.cpp kSelfCheckHash (web.page holds main.js's)
const MAGIC = 0x50574346;                          // WebProtocol.h: kMagic, kVersion, Kind, the sizes
const VERSION = 1;
const KIND = { params: 1, attach: 2, reset: 3, pull: 4, reply: 0x8000 };
const PARAMS_BYTES = 140;
const REPLY_FIXED_BYTES = 320;
const REPLY_BYTES = 320 + 32 * 512;                // sizeof(Reply): the carrier
const Q = 128;

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

const [workletPath, enginePath, loopPath] = process.argv.slice(2);
if (!workletPath || !enginePath || !loopPath) {
  console.error('usage: node worklet.mjs <fcmp-worklet.js> <fcmp-engine.wasm> <loop.js>');
  process.exit(2);
}

// ---- the stand-in scope ---------------------------------------------------------------------------------------------
// The processor's port is one end of a real MessageChannel; `page` is the other. Everything the processor posts is
// counted where it posts it (`posted`), and arrives at `page` as a browser would deliver it.
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

function construct(rate, wasm) {
  const channel = new MessageChannel();
  const side = { posted: 0, inbox: [], waiting: null, page: channel.port2 };
  const post = channel.port1.postMessage.bind(channel.port1);
  channel.port1.postMessage = (message, transfer) => {
    side.posted += 1;
    post(message, transfer);
  };
  channel.port2.on('message', (data) => {
    side.inbox.push(data);
    if (side.waiting) side.waiting();
  });
  nextPort = channel.port1;
  globalThis.sampleRate = rate;
  side.processor = new registered['fcmp-engine']({ processorOptions: { wasm } });
  return side;
}
// The next message from the processor (they arrive in the order they were posted), or null after a second.
function next(side) {
  return new Promise((done) => {
    const timer = setTimeout(() => {
      side.waiting = null;
      done(null);
    }, 1000);
    const look = () => {
      if (side.inbox.length === 0) return;
      clearTimeout(timer);
      side.waiting = null;
      done(side.inbox.shift());
    };
    side.waiting = look;
    look();
  });
}
// The processor's counters, and what it sent before them.
async function counters(side) {
  side.page.postMessage({ fcmp: 'stats' });
  const before = [];
  for (;;) {
    const m = await next(side);
    if (m === null) return { before, stats: null };
    if (m && m.fcmp === 'stats') return { before, stats: m };
    before.push(m);
  }
}
// Posts to the processor, then asks for its counters: what came back before them is what the message caused.
function exchange(side, message, transfer = []) {
  side.page.postMessage(message, transfer);
  return counters(side);
}
function record(kind, bytes, tag, size = bytes) {
  const buffer = new ArrayBuffer(size);
  const v = new DataView(buffer);
  v.setUint32(0, MAGIC, true);
  v.setUint16(4, VERSION, true);
  v.setUint16(6, kind, true);
  v.setUint32(8, bytes, true);
  v.setUint32(12, tag, true);
  return buffer;
}

const wasmFile = readFileSync(enginePath);
const wasm = wasmFile.buffer.slice(wasmFile.byteOffset, wasmFile.byteOffset + wasmFile.byteLength);
await import(pathToFileURL(workletPath).href);
const { synthLoop } = await import(pathToFileURL(loopPath).href);
row(typeof registered['fcmp-engine'] === 'function', 'registers', `processors: ${Object.keys(registered).join(', ')}`);
if (!registered['fcmp-engine']) finish();

// The module driven directly: what the script must equal.
const engineModule = new WebAssembly.Module(wasm);
function direct(rate) {
  const x = new WebAssembly.Instance(engineModule, {}).exports;
  x._initialize();
  const engine = x.fcmp_web_create();
  const at = x.malloc(4 * 1024 * 4);
  x.fcmp_web_configure(engine, rate, Q);
  const f = new Float32Array(x.memory.buffer);
  const [inL, inR, outL, outR] = [0, 1, 2, 3].map((i) => at + i * 1024 * 4);
  return { x, engine, f, inL, inR, outL, outR };
}

// ---- 1. the constructor ---------------------------------------------------------------------------------------------
const side = construct(48000, wasm);
const p = side.processor;
{
  const ready = await next(side);
  const keys = ready ? Object.keys(ready).sort().join(',') : '';
  row(!!ready && ready.fcmp === 'ready' && ready.abi === 1 && ready.latency >= 0 && ready.sampleRate === 48000
      && keys === 'abi,fcmp,latency,sampleRate' && side.posted === 1, 'ready',
      `${JSON.stringify(ready)}; ${side.posted} message(s) from the constructor`);
  row(wasm.byteLength === wasmFile.byteLength, 'ready.bytes_kept', 'the constructor leaves the engine bytes attached');
  const rates = [];
  for (const rate of [44100, 96000]) {
    const other = construct(rate, wasm);
    const m = await next(other);
    const ready = m && m.fcmp === 'ready' && m.sampleRate === rate;
    rates.push(ready ? `${rate}: latency ${m.latency}` : `${rate}: ${JSON.stringify(m)}`);
  }
  row(rates.every((r) => r.includes('latency')), 'ready.rates', rates.join('; '));
}
{
  const bad = construct(48000, new ArrayBuffer(64));
  const said = await next(bad);
  const out = [[new Float32Array(Q).fill(1), new Float32Array(Q).fill(1)]];
  let threw = '';
  let alive = false;
  try {
    alive = bad.processor.process([[new Float32Array(Q), new Float32Array(Q)]], out);
  } catch (error) {
    threw = String(error);
  }
  const { before, stats } = await exchange(bad, record(KIND.pull, 16, 1, REPLY_BYTES));
  row(!!said && said.fcmp === 'error' && typeof said.error === 'string' && said.error.length > 0 && threw === ''
      && alive === true && out[0][0][0] === 1 && before.length === 0 && !!stats && stats.ok === false
      && stats.refused === 1, 'error.said_and_silent',
      `${JSON.stringify(said)}; process() ${threw || 'returns true and writes nothing'}; a record is refused`);
}

// ---- 2. the self-check ----------------------------------------------------------------------------------------------
{
  side.page.postMessage({ fcmp: 'selfcheck' });
  const m = await next(side);
  row(!!m && m.fcmp === 'selfcheck' && m.rc === 0 && m.hash === SELF_CHECK_HASH, 'selfcheck',
      m ? `${m.hash} in ${m.ms} ms (want ${SELF_CHECK_HASH})` : 'no answer');
  // A source plays: the last quantum had an input channel. It stopped: the input has none.
  const playing = construct(48000, wasm);
  await next(playing);
  const out = [[new Float32Array(Q), new Float32Array(Q)]];
  playing.processor.process([[new Float32Array(Q), new Float32Array(Q)]], out);
  playing.page.postMessage({ fcmp: 'selfcheck' });
  const busy = await next(playing);
  playing.processor.process([[]], out);
  playing.page.postMessage({ fcmp: 'selfcheck' });
  const idle = await next(playing);
  row(!!busy && busy.rc === -2 && busy.hash === '' && !!idle && idle.rc === 0 && idle.hash === SELF_CHECK_HASH,
      'selfcheck.not_while_a_source_plays',
      `playing: rc ${busy && busy.rc}; stopped: rc ${idle && idle.rc}, ${idle && idle.hash}`);
}

// ---- 3. the wire rule -----------------------------------------------------------------------------------------------
{
  // Attach: applied, no reply.
  const attach = record(KIND.attach, 20, 7);
  new DataView(attach).setUint32(16, 1, true);
  let r = await exchange(side, attach, [attach]);
  row(r.before.length === 0 && !!r.stats && r.stats.records === 1 && r.stats.replies === 0 && r.stats.refused === 0,
      'attach.no_reply', JSON.stringify(r.stats));

  // Pull in its carrier: the record is the first Header::bytes = 16 bytes of 16,704.
  const carrier = record(KIND.pull, 16, 42, REPLY_BYTES);
  r = await exchange(side, carrier, [carrier]);
  const back = r.before[0];
  const v = back instanceof ArrayBuffer ? new DataView(back) : null;
  row(carrier.byteLength === 0 && r.before.length === 1 && !!v && back.byteLength === REPLY_BYTES
      && v.getUint32(0, true) === MAGIC && v.getUint16(6, true) === KIND.reply && v.getUint32(12, true) === 42
      && v.getUint32(8, true) === REPLY_FIXED_BYTES + 32 * v.getUint32(24, true) && !!r.stats && r.stats.replies === 1
      && r.stats.refused === 0, 'pull.reply_in_its_carrier',
      v ? `${back.byteLength} bytes back, Header::bytes ${v.getUint32(8, true)}, `
          + `flags 0x${v.getUint32(16, true).toString(16)}, latency ${v.getUint32(20, true)}, `
          + `columns ${v.getUint32(24, true)}; refused ${r.stats && r.stats.refused}`
        : `no reply; refused ${r.stats && r.stats.refused}, last ${r.stats && r.stats.lastRefusal}`);

  // The same buffer goes round again and again: 60 pulls, one carrier.
  let carried = back;
  let rounds = 0;
  for (let i = 0; i < 60 && carried instanceof ArrayBuffer && carried.byteLength === REPLY_BYTES; i += 1) {
    new Uint8Array(carried).set(new Uint8Array(record(KIND.pull, 16, 100 + i)));
    side.page.postMessage(carried, [carried]);
    carried = await next(side);
    if (carried instanceof ArrayBuffer && new DataView(carried).getUint32(12, true) === 100 + i) rounds += 1;
  }
  row(rounds === 60, 'pull.carrier_goes_round',
      `${rounds} of 60 replies came back in the one buffer, each with its Pull's tag`);

  // Refusals: nothing is sent, the count and the engine's code say what happened.
  const said = [];
  const refusal = async (name, buffer, code) => {
    const was = (await counters(side)).stats;
    const got = await exchange(side, buffer, [buffer]);
    const ok = got.before.length === 0 && !!got.stats && got.stats.refused === was.refused + 1
            && got.stats.lastRefusal === code;
    said.push(`${name}: ${got.stats ? got.stats.lastRefusal : 'no stats'}${ok ? '' : ' (WRONG)'}`);
    return ok;
  };
  const noMagic = record(KIND.pull, 16, 0, REPLY_BYTES);
  new DataView(noMagic).setUint32(0, 0, true);
  let ok = await refusal('sixteen zero bytes', new ArrayBuffer(16), -1);
  ok = await refusal('shorter than a header', new ArrayBuffer(8), -1) && ok;
  ok = await refusal('no magic', noMagic, -2) && ok;
  ok = await refusal('an unknown kind', record(9, 16, 0), -4) && ok;
  ok = await refusal('a Pull that says 20 bytes', record(KIND.pull, 20, 0, REPLY_BYTES), -5) && ok;
  ok = await refusal('a Pull in a buffer too small for its reply', record(KIND.pull, 16, 0), -5) && ok;
  row(ok, 'refused.counted_and_silent', said.join('; '));

  // The inbox keeps an earlier record's bytes. A valid Params record (every value NaN: the defaults), then a bare
  // Params header that claims 140 bytes: posting 140 would apply the old values again.
  const refusedSoFar = said.length;
  const params = record(KIND.params, PARAMS_BYTES, 0);
  new Float32Array(params, 16, 30).fill(NaN);
  new DataView(params).setUint32(136, 1, true);
  r = await exchange(side, params, [params]);
  const applied = r.before.length === 0 && !!r.stats && r.stats.refused === refusedSoFar;
  ok = await refusal('a Params header alone', record(KIND.params, PARAMS_BYTES, 0, 16), -5);
  row(applied && ok, 'refused.never_the_inbox_s_old_bytes',
      `a whole Params record is ${applied ? 'applied' : 'NOT applied'}; then ${said[said.length - 1]}`);

  // Anything else on the port is not the worklet's.
  let quiet = true;
  for (const other of [null, 7, 'stats', { fcmp: 'nonsense' }, { b: new ArrayBuffer(16), n: 16 }, new Uint8Array(16)]) {
    const got = await exchange(side, other);
    quiet = quiet && got.before.length === 0 && !!got.stats && got.stats.refused === refusedSoFar + 1;
  }
  row(quiet, 'other_messages.ignored',
      'null, a number, a string, an unknown question, an object with a buffer, a typed array');
}

// ---- 4. process() ---------------------------------------------------------------------------------------------------
const loop = synthLoop(48000);
const equal = (a, b, n) => {
  let bad = 0;
  for (let i = 0; i < n; i += 1) if (!Object.is(a[i], b[i])) bad += 1;
  return bad;
};
const postedBefore = side.posted;
const consoleCalls = { n: 0 };
const realConsole = {};
for (const k of ['log', 'info', 'warn', 'error', 'debug', 'trace']) {
  realConsole[k] = console[k];
}
const hushConsole = (on) => {
  for (const k of Object.keys(realConsole)) console[k] = on ? () => { consoleCalls.n += 1; } : realConsole[k];
};

{
  // 3000 quanta of the loop (8 s), with the Attach above: telemetry is being published.
  const d = direct(48000);
  const attach = record(KIND.attach, 20, 0);
  new DataView(attach).setUint32(16, 1, true);
  const m = d.x.malloc(20);
  new Uint8Array(d.x.memory.buffer).set(new Uint8Array(attach), m);
  d.x.fcmp_web_post(d.engine, m, 20);
  const inputs = [[new Float32Array(Q), new Float32Array(Q)]];
  const outputs = [[new Float32Array(Q), new Float32Array(Q)]];
  let bad = 0;
  let peak = 0;
  let alive = true;
  hushConsole(true);
  for (let q = 0; q < 3000; q += 1) {
    const at = (q * Q) % loop.frames;
    inputs[0][0].set(loop.left.subarray(at, at + Q));
    inputs[0][1].set(loop.right.subarray(at, at + Q));
    d.f.set(inputs[0][0], d.inL / 4);
    d.f.set(inputs[0][1], d.inR / 4);
    d.x.fcmp_web_process(d.engine, d.inL, d.inR, d.outL, d.outR, Q);
    alive = p.process(inputs, outputs) === true && alive;
    bad += equal(outputs[0][0], d.f.subarray(d.outL / 4), Q) + equal(outputs[0][1], d.f.subarray(d.outR / 4), Q);
    for (let i = 0; i < Q; i += 1) peak = Math.max(peak, Math.abs(outputs[0][0][i]));
  }
  hushConsole(false);
  row(bad === 0 && peak > 0.05 && alive, 'process.equals_the_module',
      `3000 quanta of the loop: ${bad} samples differ from the module driven directly, peak ${peak.toFixed(3)}`);
}

{
  // Any frame count: the script feeds the engine pieces of at most 128, so the module is driven with the same pieces.
  const counts = [256, 1024, 100, 1, 300, 127, 129];
  const a = construct(48000, wasm).processor;
  const d = direct(48000);
  let bad = 0;
  let at = 0;
  for (let round = 0; round < 40; round += 1) {
    for (const frames of counts) {
      const inputs = [[loop.left.slice(at, at + frames), loop.right.slice(at, at + frames)]];
      const outputs = [[new Float32Array(frames), new Float32Array(frames)]];
      a.process(inputs, outputs);
      for (let piece = 0; piece < frames; piece += Q) {
        const n = Math.min(Q, frames - piece);
        d.f.set(inputs[0][0].subarray(piece, piece + n), d.inL / 4);
        d.f.set(inputs[0][1].subarray(piece, piece + n), d.inR / 4);
        d.x.fcmp_web_process(d.engine, d.inL, d.inR, d.outL, d.outR, n);
        bad += equal(outputs[0][0].subarray(piece), d.f.subarray(d.outL / 4), n)
             + equal(outputs[0][1].subarray(piece), d.f.subarray(d.outR / 4), n);
      }
      at += frames;
    }
  }
  row(bad === 0, 'process.any_frame_count', `quanta of ${counts.join(', ')} frames, 40 rounds: ${bad} samples differ`);
}

{
  // Every shape, each against the module given the same pointers (0: no such channel).
  const shapes = [
    ['stereo to stereo', (l, r) => [[l, r]], 2, true, true],
    ['mono in', (l) => [[l]], 2, true, false],
    ['no source (an input with no channel)', () => [[]], 2, false, false],
    ['no input at all', () => [], 2, false, false],
    ['one output channel', (l, r) => [[l, r]], 1, true, true],
    ['an input of another length (not read)', (l, r) => [[l.subarray(0, 64), r.subarray(0, 64)]], 2, false, false],
  ];
  const said = [];
  let ok = true;
  for (const [name, shape, outChannels, hasL, hasR] of shapes) {
    const a = construct(48000, wasm).processor;
    const d = direct(48000);
    let bad = 0;
    let threw = '';
    for (let q = 0; q < 300; q += 1) {
      const l = loop.left.slice(q * Q, q * Q + Q);
      const r = loop.right.slice(q * Q, q * Q + Q);
      const outputs = [outChannels === 2 ? [new Float32Array(Q), new Float32Array(Q)] : [new Float32Array(Q)]];
      try {
        a.process(shape(l, r), outputs);
      } catch (error) {
        threw = String(error);
      }
      d.f.set(l, d.inL / 4);
      d.f.set(r, d.inR / 4);
      d.x.fcmp_web_process(d.engine, hasL ? d.inL : 0, hasR ? d.inR : 0, d.outL, outChannels === 2 ? d.outR : 0, Q);
      bad += equal(outputs[0][0], d.f.subarray(d.outL / 4), Q);
      if (outChannels === 2) bad += equal(outputs[0][1], d.f.subarray(d.outR / 4), Q);
    }
    ok = ok && bad === 0 && threw === '';
    said.push(`${name}: ${threw || `${bad} differ`}`);
  }
  // No output channel, no output, and a quantum of no frames: nothing to write, and no throw.
  const a = construct(48000, wasm).processor;
  let threw = '';
  try {
    a.process([[new Float32Array(Q)]], [[]]);
    a.process([[new Float32Array(Q)]], []);
    a.process([[new Float32Array(0)]], [[new Float32Array(0), new Float32Array(0)]]);
  } catch (error) {
    threw = String(error);
  }
  row(ok && threw === '', 'process.every_shape',
      `${said.join('; ')}; no output channel, no output, no frames: ${threw || 'no throw'}`);
}

{
  // An engine that traps inside process(): silence from then on, never a throw; the next message says why.
  const t = construct(48000, wasm);
  await next(t);
  const x = t.processor.x;
  const trap = () => {
    throw new WebAssembly.RuntimeError('unreachable');
  };
  t.processor.x = { fcmp_web_process: trap, fcmp_web_post: x.fcmp_web_post, fcmp_web_latency: x.fcmp_web_latency,
                    fcmp_web_selfcheck: x.fcmp_web_selfcheck };
  const outputs = [[new Float32Array(Q).fill(0.5), new Float32Array(Q).fill(0.5)]];
  const inputs = [[new Float32Array(Q), new Float32Array(Q)]];
  let threw = '';
  let alive = false;
  const postedBeforeTrap = t.posted;
  try {
    alive = t.processor.process(inputs, outputs) && t.processor.process(inputs, outputs);
  } catch (error) {
    threw = String(error);
  }
  const silent = outputs[0][0].every((s) => s === 0) && outputs[0][1].every((s) => s === 0);
  const fromProcess = t.posted - postedBeforeTrap;
  const { before, stats } = await exchange(t, { fcmp: 'nothing' });
  row(threw === '' && alive === true && silent && fromProcess === 0 && before.length === 1 && before[0].fcmp === 'error'
      && !!stats && stats.ok === false, 'process.a_trap_is_silence',
      `${threw || 'no throw'}; the output is ${silent ? 'zeros' : 'NOT zeros'}; ${fromProcess} message(s) from `
      + `process(); then on the port: ${JSON.stringify(before[0])}`);
}

// ---- no allocation --------------------------------------------------------------------------------------------------
{
  const inputs = [[new Float32Array(Q), new Float32Array(Q)]];
  const mono = [[inputs[0][0]]];
  const none = [[]];
  const outputs = [[new Float32Array(Q), new Float32Array(Q)]];
  const odd = [[new Float32Array(300), new Float32Array(300)]];
  const oddOut = [[new Float32Array(300), new Float32Array(300)]];
  inputs[0][0].set(loop.left.subarray(0, Q));
  inputs[0][1].set(loop.right.subarray(0, Q));
  odd[0][0].set(loop.left.subarray(0, 300));
  odd[0][1].set(loop.right.subarray(0, 300));
  const run = (quanta) => {
    for (let q = 0; q < quanta; q += 1) {
      p.process(inputs, outputs);
      if (q % 4 === 1) p.process(mono, outputs);
      if (q % 4 === 2) p.process(none, outputs);
      if (q % 4 === 3) p.process(odd, oddOut);
    }
  };
  // Warm first: until V8 has compiled the index loops of the odd path, its interpreter boxes every sample it reads.
  hushConsole(true);
  run(20000);
  const session = new inspector.Session();
  session.connect();
  const ask = (method, params = {}) => new Promise((done, fail) => {
    session.post(method, params, (error, result) => (error ? fail(error) : done(result)));
  });
  await ask('HeapProfiler.enable');
  await ask('HeapProfiler.startSampling', { samplingInterval: 1, includeObjectsCollectedByMajorGC: true,
                                            includeObjectsCollectedByMinorGC: true });
  const QUANTA = 20000;
  run(QUANTA);
  const { profile } = await ask('HeapProfiler.stopSampling');
  session.disconnect();
  hushConsole(false);
  // Every byte allocated under a frame of the worklet script (the frame itself and whatever it called).
  const total = (node) => node.selfSize + node.children.reduce((sum, child) => sum + total(child), 0);
  const where = [];
  let everything = 0;
  const walk = (node) => {
    everything += node.selfSize;
    if (node.callFrame.url.endsWith('fcmp-worklet.js')) {
      const bytes = total(node);
      const frame = node.callFrame;
      if (bytes > 0) where.push(`${frame.functionName || '(anonymous)'} line ${frame.lineNumber + 1}: ${bytes} bytes`);
      everything += bytes - node.selfSize;
      return bytes;
    }
    return node.children.reduce((sum, child) => sum + walk(child), 0);
  };
  const bytes = walk(profile.head);
  const calls = QUANTA + 3 * QUANTA / 4;
  row(bytes === 0, 'process.no_allocation',
      `${bytes} bytes allocated under fcmp-worklet.js over ${calls} calls of process() (128 frames, mono, no source, `
      + '300 frames)'
      + `${where.length ? ': ' + where.join('; ') : ''}; elsewhere in this test: ${everything - bytes} bytes`);
  row(side.posted === postedBefore && consoleCalls.n === 0, 'process.says_nothing',
      `${side.posted - postedBefore} messages and ${consoleCalls.n} console calls from ${3000 + 2 * calls} calls of `
      + 'process()');
}

// ---- what it costs (a note: this runs beside other tests) -----------------------------------------------------------
{
  const d = direct(48000);
  const inputs = [[new Float32Array(Q), new Float32Array(Q)]];
  const outputs = [[new Float32Array(Q), new Float32Array(Q)]];
  const reps = 10000;
  const t0 = performance.now();
  for (let q = 0; q < reps; q += 1) {
    const at = (q * Q) % loop.frames;
    inputs[0][0].set(loop.left.subarray(at, at + Q));
    inputs[0][1].set(loop.right.subarray(at, at + Q));
    p.process(inputs, outputs);
  }
  const t1 = performance.now();
  for (let q = 0; q < reps; q += 1) {
    const at = (q * Q) % loop.frames;
    d.f.set(loop.left.subarray(at, at + Q), d.inL / 4);
    d.f.set(loop.right.subarray(at, at + Q), d.inR / 4);
    d.x.fcmp_web_process(d.engine, d.inL, d.inR, d.outL, d.outR, Q);
  }
  const t2 = performance.now();
  const script = (t1 - t0) / reps * 1000;
  note(`process(): ${script.toFixed(1)} us a quantum through the script, ${((t2 - t1) / reps * 1000).toFixed(1)} us `
       + 'for the module alone (default Mode, STD, the loop at 48 kHz); a quantum lasts 2667 us: '
       + `${(script / 26.67).toFixed(2)} % of real time`);
}

finish();
