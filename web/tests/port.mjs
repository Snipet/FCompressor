// web/tests/port.mjs: the editor module's link to the worklet, PortLink (Source/web/ui/PortLink.h), under node
// (web Sprint D, ADR-93).
//
// FCMP_WEB_TEST name=web.ui.port timeout=120 args={build},{engine}
//
//   node port.mjs <build directory> <fcmp-engine.wasm>
//
// <build>/fcmp-port-check.mjs is PortLink.cpp, the browser module's own file, with a WebFacade, wired as WebMain wires
// them (Tools/web/port/portcheck.cpp). This script is the rest of the page: it hands the link one end of a real
// MessageChannel, as the page hands it the AudioWorkletNode's port, and puts a worklet stand-in over the shipped
// fcmp-engine.wasm on the other end.
//
// The stand-in (WorkletStandIn, below) is this file's own and follows the wire rule of docs/sprints/web-d.md ("The
// seam between the module and the page"), not the page's worklet script: a message whose data is an ArrayBuffer is one
// record; it copies at most 140 bytes to its inbox, posts n = Header::bytes (the u32 at offset 8, the one field a
// script reads), and when fcmp_web_post returns r > 0 copies r bytes of the reply into the same buffer and transfers
// it back; r < 0 is counted as a refusal and nothing is sent. It runs on this thread, over the channel, and not in a
// worker_threads Worker: a MessageChannel serialises, transfers and delivers in a later task whichever thread holds
// its ends, which is all the link can see of a worklet, and on one thread the script decides when the engine renders
// and when a reply leaves, so a late reply and a worklet that stops answering are made on purpose instead of raced
// for.
//
// Rows (each a rule of PortLink.h):
//   unconnected.dropped         before a port exists a record is dropped and counted; nothing throws
//   connect.events              connect() calls Events::connected with the rate and the block; the port's onmessage is
//                               the link's
//   connect.resync              inside connect() the facade's values (a snap) and its Attach go out, and the engine
//                               takes both
//   frames.replies              60 frames, a Pull each: 60 replies, none refused, each in a later task than its pull()
//   frames.one_carrier          and one carrier allocated for all of them: by the link's count, and seen from the
//                               port, where each Pull after the first goes out in the very buffer the last reply came
//                               back in
//   frames.flags                the reply's flags: a frame, configured, attached
//   frames.telemetry            columns in the facade's mirror, a published UiFrame
//   frames.diagnostics          prepared, the connected rate and block, the engine's latency
//   wire.transferred            everything the link posted was an ArrayBuffer of its own, transferred (detached here)
//   wire.sizes                  a Pull in 16,704 bytes, every other record in exactly Header::bytes; a Reset, 16 bytes
//                               like a Pull, travels in 16
//   wire.heap_view              the trap: nothing arrived that was a view, or a buffer of another size (a view on the
//                               wasm heap posted without a slice arrives as a clone of the whole memory)
//   wire.heap_view_is_real      what that clone is: 16 bytes of a view over a WebAssembly memory, posted as they are
//   private.not_a_record        a message that is no ArrayBuffer is the page's, even a typed array holding a whole
//                               reply: the link does not take it, a listener added with addEventListener does
//   reconfigure.through_port    QUALITY and LOOKAHEAD written in the editor reconfigure the engine in its handler: the
//                               latency in the next reply is the engine's new one
//   replaced.events             connect() on a second port: disconnected, then connected, with the new rate
//   replaced.late_reply         the old port's reply, arriving afterwards, is ignored and counted
//   replaced.resynced           the new engine has the facade's values: the same latency
//   lost.pull_patience          a worklet that stops answering: 61 pull() calls post 3 Pulls and allocate 2 carriers
//                               (the first travels in the buffer that had come back, the other two in new ones)
//   lost.extras_dropped         its late answers are taken, one carrier is kept, and the next frames allocate none:
//                               each travels in a buffer that had come back
//   disconnected.dropped        after disconnect() records are dropped and counted, and the port's late reply ignored
//   reconnect.same_port         connect() with that port again: resync, replies
//   destroyed.late_reply        with the link gone a reply still arrives on its port (the page hears it): nothing
//                               is called in the module and nothing is written (the check module watches the
//                               destroyed link's bytes, the place of its inbox and the bytes at address 0:
//                               Tools/web/port/portcheck.cpp)
//   destroyed.inert             and Module.fcmpPort does nothing (a port given to connect() is left alone)
//   displaced.inert             a second link in the module (a second instance of the check module, so that the rows
//                               above keep their one link) takes the name Module.fcmpPort; the first is disconnected
//                               and inert: a record on its port is not delivered, what it posts is dropped, its
//                               connect() does nothing
//   displaced.destroyed         the first link destroyed while displaced: a record on its port touches nothing, and
//                               the second link, connected through the name, takes its own
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { MessageChannel } from 'node:worker_threads';

const TEST = 'web.ui.port';

// WebProtocol.h's sizes. Header::bytes is the u32 at offset 8.
const CARRIER_BYTES = 16704;                   // sizeof(Reply)
const MAX_MESSAGE_BYTES = 140;                 // kMaxMessageBytes: a Params record
const PARAMS_BYTES = 140, ATTACH_BYTES = 20, RESET_BYTES = 16, PULL_BYTES = 16;
const REPLY_FRAME = 1 << 2, REPLY_CONFIGURED = 1 << 4, REPLY_ATTACHED = 1 << 5;
const QUANTUM = 128;
const PULL_PATIENCE = 30;                      // WebFacade::kPullPatience

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

const [buildDir, enginePath] = process.argv.slice(2);
if (!buildDir || !enginePath) {
  console.error('usage: node port.mjs <build directory> <fcmp-engine.wasm>');
  process.exit(2);
}
const checkPath = join(buildDir, 'fcmp-port-check.mjs');
if (!existsSync(checkPath) || !existsSync(enginePath)) {
  row(false, 'built', `${existsSync(checkPath) ? enginePath : checkPath} is not built`);
  finish();
}

// A message crosses the channel in a later task: wait for what it must cause, never for a number of turns.
const turn = () => new Promise((done) => setImmediate(done));
async function until(condition, seconds = 5) {
  const end = performance.now() + seconds * 1000;
  while (!condition()) {
    if (performance.now() > end) return false;
    await turn();
  }
  return true;
}
// For what must NOT happen there is nothing to wait for: let every queued message task run.
async function settle() {
  for (let i = 0; i < 20; i += 1) await new Promise((done) => setTimeout(done, 1));
}

// ---- the worklet's side ---------------------------------------------------------------------------------------------
const engineModule = new WebAssembly.Module(readFileSync(enginePath));

class WorkletStandIn {
  constructor(port, sampleRate) {
    this.port = port;
    const x = new WebAssembly.Instance(engineModule, {}).exports;     // an empty import object, as the worklet does
    x._initialize();
    this.x = x;
    this.engine = x.fcmp_web_create();
    this.inbox = x.malloc(MAX_MESSAGE_BYTES);
    this.audio = x.malloc(4 * QUANTUM * 4);                           // inL, inR, outL, outR
    this.configuredLatency = x.fcmp_web_configure(this.engine, sampleRate, QUANTUM);
    this.bytes = new Uint8Array(x.memory.buffer);                     // the engine's memory never grows
    this.words = new Uint32Array(x.memory.buffer);
    this.floats = new Float32Array(x.memory.buffer);
    this.records = [];        // every ArrayBuffer that arrived: { byteLength, n, r }
    this.others = [];         // everything else that arrived, described
    this.refused = 0;         // fcmp_web_post said no
    this.short = 0;           // a reply that did not fit the buffer its Pull came in
    this.answering = true;    // false: records are taken off the port and kept, unanswered
    this.kept = [];
    this.holdReplies = false; // true: a reply is written into its buffer and held back
    this.held = [];
    this.lastReply = null;    // a copy of the last reply's bytes
    this.phase = 0;
    port.onmessage = (event) => this.message(event.data);
  }

  message(data) {
    if (!(data instanceof ArrayBuffer)) {
      this.others.push(ArrayBuffer.isView(data)
        ? `a ${data.constructor.name} of ${data.byteLength} bytes over a buffer of ${data.buffer.byteLength}`
        : JSON.stringify(data));
      return;
    }
    if (!this.answering) {
      this.kept.push(data);
      return;
    }
    const x = this.x;
    const copied = Math.min(data.byteLength, MAX_MESSAGE_BYTES);
    this.bytes.set(new Uint8Array(data, 0, copied), this.inbox);
    const n = Math.min(this.words[(this.inbox >> 2) + 2], copied);
    const r = x.fcmp_web_post(this.engine, this.inbox, n);
    this.records.push({ byteLength: data.byteLength, n, r });
    if (r < 0) {
      this.refused += 1;
    } else if (r > data.byteLength) {
      this.short += 1;
    } else if (r > 0) {
      const reply = x.fcmp_web_reply(this.engine);
      new Uint8Array(data, 0, r).set(this.bytes.subarray(reply, reply + r));
      this.lastReply = this.bytes.slice(reply, reply + r);
      if (this.holdReplies) this.held.push(data);
      else this.port.postMessage(data, [data]);
    }
  }

  answerKept() {
    this.answering = true;
    for (const data of this.kept.splice(0)) this.message(data);
  }

  release() {
    this.holdReplies = false;
    for (const data of this.held.splice(0)) this.port.postMessage(data, [data]);
  }

  // `quanta` render quanta of a stereo tone, as the AudioWorklet's process() would run them.
  process(quanta) {
    const [inL, inR, outL, outR] = [0, 1, 2, 3].map((i) => this.audio + i * QUANTUM * 4);
    for (let q = 0; q < quanta; q += 1) {
      for (let i = 0; i < QUANTUM; i += 1) {
        const s = 0.5 * Math.sin(this.phase);
        this.phase += 0.13;
        this.floats[(inL >> 2) + i] = s;
        this.floats[(inR >> 2) + i] = -s;
      }
      this.x.fcmp_web_process(this.engine, inL, inR, outL, outR, QUANTUM);
    }
  }

  latency() {
    return this.x.fcmp_web_latency(this.engine);
  }
}

// What the link hands the port: every postMessage call, seen before and after the port takes it. And whose buffer a
// carrier is, seen from this side of the port and not by the link's own count: `returned` when it is one that arrived
// on this port in a reply and has not gone out since, `last` when it is the last one that arrived. (A listener and the
// port's onmessage are given the same event, so the same ArrayBuffer object.)
function watchPosts(port) {
  const posts = [];
  const came = new Set();
  let lastCame = null;
  port.addEventListener('message', (event) => {
    if (!(event.data instanceof ArrayBuffer) || event.data.byteLength !== CARRIER_BYTES) return;
    came.add(event.data);
    lastCame = event.data;
  });
  const post = port.postMessage.bind(port);
  port.postMessage = (message, transfer) => {
    const isBuffer = message instanceof ArrayBuffer;
    const entry = {
      isBuffer,
      bytes: isBuffer || ArrayBuffer.isView(message) ? message.byteLength : -1,
      listed: Array.isArray(transfer) && transfer.length === 1 && transfer[0] === message,
      detached: false,
      returned: came.delete(message),
      last: message === lastCame,
    };
    post(message, transfer);
    entry.detached = isBuffer && message.byteLength === 0;
    posts.push(entry);
  };
  return posts;
}

// The carriers a port was handed, in order: R for a buffer that had come back, n for one that had not.
const carriers = (w) => w.posts.filter((p) => p.bytes === CARRIER_BYTES).map((p) => (p.returned ? 'R' : 'n')).join('');

function worklet(sampleRate) {
  const channel = new MessageChannel();
  const posts = watchPosts(channel.port1);
  return { page: channel.port1, posts, standIn: new WorkletStandIn(channel.port2, sampleRate) };
}

// ---- the module -----------------------------------------------------------------------------------------------------
const instantiate = (await import(pathToFileURL(checkPath).href)).default;
const M = await instantiate();
const check = M.fcmpCheck;
const link = M.fcmpPort;
if (!row(!!check && !!link && typeof link.connect === 'function' && typeof link.disconnect === 'function',
         'module.names', 'Module.fcmpPort.connect and .disconnect, and the check hooks'))
  finish();
const PID = check.status().pid;

// One frame of the page: a Pull, some audio, the reply. A reply already there when pull() returns did not come from a
// message task.
let repliesInsideCalls = 0;
async function frame(w, quanta = 6) {
  const before = check.status().replies;
  check.pull();
  if (check.status().replies !== before) repliesInsideCalls += 1;
  w.standIn.process(quanta);
  return until(() => check.status().replies > before);
}
async function frames(w, count) {
  let answered = 0;
  for (let f = 0; f < count; f += 1) if (await frame(w)) answered += 1;
  return answered;
}

// ---- 1. before any port ---------------------------------------------------------------------------------------------
check.attach(1);
check.pull();
check.pull();                                  // waits for the first one: nothing posted, nothing dropped
check.set(PID.thr, 0.4);
{
  const s = check.status();
  row(s.posted === 0 && s.dropped === 3 && s.replies === 0 && s.connected === 0, 'unconnected.dropped',
      `posted ${s.posted}, dropped ${s.dropped} (an Attach, a Pull, a Params record)`);
}

// ---- 2. connect -----------------------------------------------------------------------------------------------------
const a = worklet(48000);
link.connect(a.page, 48000, QUANTUM);
{
  const s = check.status();
  row(s.connects === 1 && s.disconnects === 0 && s.eventRate === 48000 && s.eventBlock === QUANTUM
      && s.connected === 1 && typeof a.page.onmessage === 'function', 'connect.events',
      `connected(${s.eventRate}, ${s.eventBlock}) called ${s.connects} time(s); the port's onmessage is set`);
  const arrived = await until(() => a.standIn.records.length >= 2);
  const sizes = a.standIn.records.map((r) => r.byteLength).join(', ');
  row(s.posted === 2 && s.dropped === 3 && arrived && sizes === `${PARAMS_BYTES}, ${ATTACH_BYTES}`
      && a.standIn.records.every((r) => r.r === 0) && a.standIn.refused === 0, 'connect.resync',
      `posted ${s.posted} inside connect(); the engine took buffers of ${sizes || 'nothing'} bytes, `
      + `refused ${a.standIn.refused}`);
}

// ---- 3. frames: a Pull per frame, six quanta of audio between frames ------------------------------------------------
{
  const answered = await frames(a, 60);
  const s = check.status();
  row(answered === 60 && s.replies === 60 && s.refused === 0 && s.delivered === 60 && a.standIn.refused === 0
      && a.standIn.short === 0 && repliesInsideCalls === 0, 'frames.replies',
      `${s.replies} replies to 60 pulls, ${s.refused} refused by the facade, ${a.standIn.refused} by the engine, `
      + `${repliesInsideCalls} inside a pull() call`);
  const carried = a.posts.filter((p) => p.bytes === CARRIER_BYTES);
  const same = carried.filter((p) => p.returned && p.last).length;
  row(s.carriers === 1 && carried.length === 60 && !carried[0].returned && same === 59, 'frames.one_carrier',
      `${s.carriers} carrier(s) of ${CARRIER_BYTES} bytes allocated for 60 pulls by the link's count; at the port, `
      + `${same} of the 59 Pulls after the first went out in the buffer the last reply came back in`);
  const want = REPLY_FRAME | REPLY_CONFIGURED | REPLY_ATTACHED;
  row((s.flags & want) === want, 'frames.flags', `0x${s.flags.toString(16)} (a frame, configured, attached)`);
  row(s.written > 500 && s.publish > 0, 'frames.telemetry',
      `${s.written} columns in the mirror, publishCount ${s.publish}`);
  row(s.prepared === 1 && s.rate === 48000 && s.block === QUANTUM && s.latency === a.standIn.latency(),
      'frames.diagnostics',
      `prepared ${s.prepared}, ${s.rate} Hz, blocks of ${s.block}, latency ${s.latency} `
      + `(the engine: ${a.standIn.latency()})`);
}

// ---- 4. the wire rule -----------------------------------------------------------------------------------------------
{
  const resets = a.standIn.records.length;
  check.resetEngine();
  const arrived = await until(() => a.standIn.records.length > resets);
  const reset = a.standIn.records[resets] ?? { byteLength: -1, r: -1 };
  const bad = a.posts.filter((p) => !p.isBuffer || !p.listed || !p.detached);
  row(a.posts.length === 63 && bad.length === 0, 'wire.transferred',
      `${a.posts.length} posts (Params, Attach, 60 Pulls, Reset), ${bad.length} of them not an ArrayBuffer `
      + `transferred with itself`);
  const pulls = a.standIn.records.filter((r) => r.r > 0);
  const others = a.standIn.records.filter((r) => r.r <= 0);
  row(arrived && pulls.length === 60 && pulls.every((r) => r.byteLength === CARRIER_BYTES && r.n === PULL_BYTES)
      && others.every((r) => r.byteLength === r.n) && reset.byteLength === RESET_BYTES && reset.r === 0, 'wire.sizes',
      `${pulls.length} Pulls in ${CARRIER_BYTES} bytes; the others in ${others.map((r) => r.byteLength).join(', ')} `
      + `(Header::bytes each); the Reset in ${reset.byteLength}`);
  const sizes = [...new Set(a.standIn.records.map((r) => r.byteLength))].sort((p, q) => p - q);
  const allowed = [CARRIER_BYTES, PARAMS_BYTES, ATTACH_BYTES, RESET_BYTES];
  row(a.standIn.others.length === 0 && sizes.every((n) => allowed.includes(n)), 'wire.heap_view',
      a.standIn.others.length === 0 ? `every message was an ArrayBuffer of ${sizes.join(', ')} bytes`
                                    : `the worklet received ${a.standIn.others[0]}`);

  // The trap itself, with a memory of this script's own: a view posted as it is brings the whole memory with it.
  const channel = new MessageChannel();
  const got = new Promise((done) => { channel.port2.onmessage = (event) => done(event.data); });
  const memory = new WebAssembly.Memory({ initial: 256 });
  channel.port1.postMessage(new Uint8Array(memory.buffer).subarray(64, 80));
  const clone = await got;
  row(clone.byteLength === 16 && clone.buffer.byteLength === memory.buffer.byteLength, 'wire.heap_view_is_real',
      `a 16-byte view of a wasm memory, posted without a slice, arrived over a buffer of `
      + `${clone.buffer.byteLength} bytes`);
  channel.port1.close();
}

// ---- 5. the port's other messages are the page's --------------------------------------------------------------------
{
  const heard = [];
  a.page.addEventListener('message', (event) => {
    if (!(event.data instanceof ArrayBuffer)) heard.push(event.data);
  });
  const before = check.status();
  a.standIn.port.postMessage({ fcmp: 'stats', quanta: 360 });
  a.standIn.port.postMessage(a.standIn.lastReply);         // a Uint8Array: a reply's bytes, but not a record
  const arrived = await until(() => heard.length === 2);
  await settle();
  const s = check.status();
  row(arrived && heard[0].fcmp === 'stats' && heard[1] instanceof Uint8Array && s.delivered === before.delivered
      && s.replies === before.replies && s.refused === before.refused && s.ignored === 0, 'private.not_a_record',
      `the page heard ${JSON.stringify(heard[0])} and a ${heard[1]?.constructor.name} of ${heard[1]?.byteLength} `
      + `bytes; the link delivered ${s.delivered - before.delivered} of them`);
}

// ---- 6. a reconfigure through the port ------------------------------------------------------------------------------
{
  const before = check.status();
  const records = a.standIn.records.length;
  check.set(PID.thr, 0.2);
  check.set(PID.quality, 1.0);                 // the highest quality: another oversampler, another latency
  check.set(PID.labudget, 1.0);                // and the largest lookahead budget
  const arrived = await until(() => a.standIn.records.length >= records + 3);
  const answered = await frame(a);
  const s = check.status();
  row(arrived && answered && s.latency !== before.latency && s.latency === a.standIn.latency()
      && a.standIn.refused === 0 && s.quality !== before.quality && s.budget !== before.budget,
      'reconfigure.through_port',
      `latency ${before.latency} -> ${s.latency} samples (the engine: ${a.standIn.latency()}), quality `
      + `${before.quality} -> ${s.quality}, budget ${before.budget} -> ${s.budget}`);
}

// ---- 7. a new worklet: the old port's late reply is another engine's ------------------------------------------------
const b = worklet(44100);
{
  a.standIn.holdReplies = true;
  check.pull();                                // out on the old port
  const taken = await until(() => a.standIn.held.length === 1);
  const before = check.status();
  link.connect(b.page, 44100, QUANTUM);
  const now = check.status();
  row(taken && now.disconnects === 1 && now.connects === 2 && now.eventRate === 44100 && now.connected === 1,
      'replaced.events',
      `disconnected() ${now.disconnects}, connected() ${now.connects}, the last with ${now.eventRate} Hz`);
  a.standIn.release();                         // the old worklet answers now
  const seen = await until(() => check.status().ignored === 1);
  await settle();
  const s = check.status();
  row(seen && s.ignored === 1 && s.replies === before.replies && s.delivered === before.delivered
      && s.refused === before.refused, 'replaced.late_reply',
      `ignored ${s.ignored}; replies still ${s.replies}, delivered still ${s.delivered}`);
  const resynced = await until(() => b.standIn.records.length >= 2);
  const answered = await frames(b, 5);
  const end = check.status();
  row(resynced && answered === 5 && end.replies === before.replies + 5 && end.rate === 44100
      && b.standIn.refused === 0 && end.latency === b.standIn.latency()
      && b.standIn.latency() !== b.standIn.configuredLatency, 'replaced.resynced',
      `the new engine took ${b.standIn.records.slice(0, 2).map((r) => r.byteLength).join(' and ')} bytes: latency `
      + `${b.standIn.configuredLatency} at its defaults, ${b.standIn.latency()} now, the facade's ${end.latency}; `
      + `${end.rate} Hz`);
}

// ---- 8. a worklet that stops answering: the patience rule -----------------------------------------------------------
{
  b.standIn.answering = false;
  const before = check.status();
  for (let f = 0; f < 2 * PULL_PATIENCE + 1; f += 1) check.pull();
  const taken = await until(() => b.standIn.kept.length === 3);
  const s = check.status();
  const sent = carriers(b).slice(-3);
  row(taken && s.posted === before.posted + 3 && s.carriers === before.carriers + 2 && s.replies === before.replies
      && sent === 'Rnn', 'lost.pull_patience',
      `${2 * PULL_PATIENCE + 1} unanswered pull() calls posted ${s.posted - before.posted} Pulls and allocated `
      + `${s.carriers - before.carriers} more carriers; at the port: ${sent} (R: a buffer that had come back)`);
  b.standIn.answerKept();                      // all three, late
  const late = await until(() => check.status().replies === before.replies + 3);
  const answered = await frames(b, 5);
  const end = check.status();
  const next = carriers(b).slice(-5);
  row(late && answered === 5 && end.carriers === s.carriers && end.refused === before.refused && next === 'RRRRR',
      'lost.extras_dropped',
      `the three late replies were taken; 5 more frames allocated ${end.carriers - s.carriers} carriers; at the `
      + `port: ${next}`);
}

// ---- 9. disconnect, and the same port again -------------------------------------------------------------------------
{
  b.standIn.holdReplies = true;
  check.pull();
  const taken = await until(() => b.standIn.held.length === 1);
  const before = check.status();
  link.disconnect();
  link.disconnect();                           // a second one is nothing
  check.set(PID.thr, 0.6);
  check.resetEngine();
  b.standIn.release();
  const seen = await until(() => check.status().ignored === before.ignored + 1);
  await settle();
  const s = check.status();
  row(taken && seen && s.connected === 0 && s.disconnects === before.disconnects + 1 && s.posted === before.posted
      && s.dropped === before.dropped + 2 && s.replies === before.replies, 'disconnected.dropped',
      `disconnected() ${s.disconnects - before.disconnects} time(s); 2 records dropped (${s.dropped} in all), none `
      + `posted; the port's late reply ignored (${s.ignored} in all)`);
  const records = b.standIn.records.length;
  link.connect(b.page, 44100, QUANTUM);
  const resynced = await until(() => b.standIn.records.length >= records + 2);
  const answered = await frames(b, 3);
  const end = check.status();
  row(resynced && answered === 3 && end.connected === 1 && end.posted === s.posted + 5 && b.standIn.refused === 0,
      'reconnect.same_port',
      `connected again: ${end.posted - s.posted} records posted (Params, Attach, 3 Pulls), ${answered} replies`);
}

// ---- 10. the link gone ----------------------------------------------------------------------------------------------
// A call into a destroyed link throws nothing by itself: the check module says what was touched, and a trap, which
// node reports as an uncaught exception of the message task, is caught here and fails the row.
const trapped = [];
process.on('uncaughtException', (error) => trapped.push(String(error)));
const untouched = (s) => s.alive === 0 && s.watching === 1 && s.touched.link === 0 && s.touched.inbox === 0
                         && s.touched.null === 0;
const touched = (s) => `the destroyed link's bytes ${s.touched?.link}, its inbox's place ${s.touched?.inbox} `
                       + `(watched: ${s.watching}), address 0 ${s.touched?.null} changed; `
                       + `${trapped.length === 0 ? 'no trap' : trapped[0]}`;
{
  b.standIn.holdReplies = true;
  check.pull();
  const taken = await until(() => b.standIn.held.length === 1);
  let heard = 0;                               // the page's own listener: the late reply did arrive
  b.page.addEventListener('message', (event) => {
    if (event.data instanceof ArrayBuffer) heard += 1;
  });
  check.destroy();
  b.standIn.release();                         // a reply to a link that no longer exists
  const arrived = await until(() => heard === 1);
  await settle();
  const s = check.status();
  row(taken && arrived && untouched(s) && trapped.length === 0 && link.link === 0 && link.port === null
      && link.carrier === null, 'destroyed.late_reply',
      `${heard} reply of ${CARRIER_BYTES} bytes arrived on the destroyed link's port: ${touched(s)}`);

  const c = worklet(48000);
  let threw = '';
  try {
    M.fcmpPort.connect(c.page, 48000, QUANTUM);
    M.fcmpPort.disconnect();
    await settle();
  } catch (error) {
    threw = String(error);
  }
  const handler = typeof c.page.onmessage === 'function';
  const end = check.status();
  row(threw === '' && untouched(end) && trapped.length === 0 && !handler && c.posts.length === 0, 'destroyed.inert',
      threw || `connect() and disconnect() after the link was destroyed: the port given has `
               + `${handler ? 'a' : 'no'} handler and carried ${c.posts.length} posts`);
  c.page.close();
}

// ---- 11. a second link in one module --------------------------------------------------------------------------------
{
  const M2 = await instantiate();
  const check2 = M2.fcmpCheck;
  const first = M2.fcmpPort;                   // what a page kept of the first link
  const p = new MessageChannel();              // port1 is the page's end, as above; this script is the worklet
  let heard = 0;
  p.port1.addEventListener('message', (event) => {
    if (event.data instanceof ArrayBuffer) heard += 1;
  });
  const send = (fill) => {
    const buffer = new ArrayBuffer(CARRIER_BYTES);
    new Uint8Array(buffer).fill(fill);
    p.port2.postMessage(buffer, [buffer]);
    const want = heard + 1;
    return until(() => heard === want);
  };
  first.connect(p.port1, 48000, QUANTUM);
  const live = (await send(0)) && check2.status().delivered === 1;      // the handler is live: one record delivered
  const before = check2.status();

  check2.second();
  const named = M2.fcmpPort !== first && first.link === 0 && first.port === null && first.carrier === null;
  const arrived = await send(0);
  await settle();
  check2.pull();
  const q = new MessageChannel();
  first.connect(q.port1, 44100, QUANTUM);
  first.disconnect();
  const s = check2.status();
  row(live && named && arrived && s.second.alive === 1 && s.connected === 0
      && s.disconnects === before.disconnects + 1 && s.connects === before.connects
      && s.delivered === before.delivered && s.ignored === before.ignored && s.posted === before.posted
      && s.dropped === before.dropped + 1 && typeof q.port1.onmessage !== 'function', 'displaced.inert',
      `a second link took the name; the first: ${named ? 'no link, port or carrier in its state' : 'state kept'}, `
      + `disconnected() ${s.disconnects - before.disconnects} time(s), delivered `
      + `${s.delivered - before.delivered} and ignored ${s.ignored - before.ignored} of 1 record on its port, `
      + `posted ${s.posted - before.posted} and dropped ${s.dropped - before.dropped} of 1 Pull, connect() `
      + `${s.connects - before.connects} time(s)`);

  check2.destroy();                            // the first link, displaced, goes
  const late = await send(0x3c);
  await settle();
  const gone = check2.status();
  const r = new MessageChannel();
  M2.fcmpPort.connect(r.port1, 44100, QUANTUM);
  const mine = new ArrayBuffer(PULL_BYTES);
  r.port2.postMessage(mine, [mine]);
  const taken = await until(() => check2.status().second.records === 1);
  await settle();
  const end = check2.status();
  row(late && untouched(gone) && untouched(end) && trapped.length === 0 && taken && end.second.connected === 1
      && end.second.connects === 1 && end.second.records === 1, 'displaced.destroyed',
      `a record on the port of the first link, destroyed while displaced: ${touched(end)}; the second link: `
      + `connected() ${end.second.connects} time(s), ${end.second.records} record(s) of its own port`);
  for (const port of [p.port1, q.port1, r.port1]) port.close();
}

note(`port A: ${a.standIn.records.length} records, port B: ${b.standIn.records.length}, through the shipped engine; `
     + `port B's carriers: ${carriers(b)}`);
finish();
