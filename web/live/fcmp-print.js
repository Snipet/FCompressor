// Scratch prototype (scout-l/audio): dsp.print's rows through the SHIPPED worklet (fcmp-worklet.js over
// fcmp-engine.wasm) in an OfflineAudioContext. The material, the Params records and the hash come from the test-only
// module fcmp-print.wasm (PrintProgram.h compiled as it is); this script only moves bytes.
const TEST = 'web.live.print';
const QUANTUM = 128;
const logEl = document.getElementById('funkgui-log');
let failure = '';
let passed = 0, failed = 0;
const say = (line) => { logEl.textContent += `${line}\n`; };
const row = (ok, name, detail = '') => {
  if (!ok && failure === '') failure = name;
  ok ? (passed += 1) : (failed += 1);
  say(`${ok ? 'PASS' : 'FAIL'}     ${TEST} ${name}${detail ? ': ' + detail : ''}`);
  return ok;
};
const note = (text) => say(`NOTE     ${text}`);
window.addEventListener('error', (e) => { row(false, 'uncaught', e.message); document.title = `FAIL: uncaught`; });
window.addEventListener('unhandledrejection', (e) => { row(false, 'uncaught', String(e.reason)); document.title = 'FAIL: uncaught'; });

const within = (ms, what, promise) => {
  let timer = 0;
  const late = new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(`${what}: no answer in ${ms} ms`)), ms); });
  return Promise.race([promise, late]).finally(() => clearTimeout(timer));
};
const makeNode = (ctx, wasm) => within(10000, 'the audio processor', new Promise((made, bad) => {
  const n = new AudioWorkletNode(ctx, 'fcmp-engine', {
    numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit',
    channelInterpretation: 'speakers', processorOptions: { wasm } });
  n.onprocessorerror = () => bad(new Error('the audio processor stopped'));
  const hear = (event) => {
    const m = event.data;
    if (!m || (m.fcmp !== 'ready' && m.fcmp !== 'error')) return;
    n.port.removeEventListener('message', hear);
    if (m.fcmp === 'ready') made({ node: n, ready: m }); else bad(new Error(m.error));
  };
  n.port.addEventListener('message', hear);
  n.port.start();
}));
const ask = (n, what) => within(10000, `the worklet's ${what}`, new Promise((answered) => {
  const hear = (event) => {
    if (!event.data || event.data.fcmp !== what) return;
    n.port.removeEventListener('message', hear);
    answered(event.data);
  };
  n.port.addEventListener('message', hear);
  n.port.postMessage({ fcmp: what });
}));
const hex8 = (v) => v.toString(16).padStart(8, '0');

async function main() {
  const cfg = await (await fetch('fcmp-print-config.json')).json();
  note(navigator.userAgent);
  note(`config ${JSON.stringify(cfg)}`);
  const golden = await (await fetch('fcmp-print-golden.json')).json();
  const expect = await (await fetch('fcmp-print-expect.json')).json();
  const engineBytes = await (await fetch('fcmp-engine.wasm')).arrayBuffer();
  const p = (await WebAssembly.instantiate(await (await fetch('fcmp-print.wasm')).arrayBuffer(), {})).instance.exports;
  p._initialize();
  const frames = p.fcmp_print_frames();
  const pL = p.malloc(frames * 4), pR = p.malloc(frames * 4), pRec = p.malloc(140), pHash = p.malloc(8);
  const u8 = new Uint8Array(p.memory.buffer), u32 = new Uint32Array(p.memory.buffer), f32 = new Float32Array(p.memory.buffer);
  const cstr = (at) => { let s = ''; for (let i = at; u8[i] !== 0; i += 1) s += String.fromCharCode(u8[i]); return s; };
  const t0 = performance.now();
  p.fcmp_print_program(pL, pR);
  const inL = f32.slice(pL / 4, pL / 4 + frames), inR = f32.slice(pR / 4, pR / 4 + frames);
  note(`program: ${frames} frames made in ${(performance.now() - t0).toFixed(0)} ms by fcmp-print.wasm`);
  const hash = (samples) => {
    f32.set(samples, pL / 4);
    p.fcmp_print_hash(pL, samples.length, pHash);
    return hex8(u32[pHash / 4 + 1]) + hex8(u32[pHash / 4]);
  };
  // The input is the golden program: its own hash is checked against the native value held by the expectation file.
  const inHash = `${hash(inL)} ${hash(inR)}`;
  row(inHash === cfg.programHash, 'program.hash', `${inHash} (want ${cfg.programHash})`);
  const record = (mode, set, quality, budget, look, snap) => {
    if (p.fcmp_print_record(mode, set, quality, budget, look, snap, pRec) !== 140) throw new Error('no record');
    return u8.slice(pRec, pRec + 140).buffer;
  };
  const SETUP = { std: [1, 0, -1], eco: [0, 0, -1], hq: [2, 0, -1], hqla: [2, 1, 2] };
  const modes = [];
  for (let i = 0; i < p.fcmp_print_modes(); i += 1) modes.push(cstr(p.fcmp_print_key(i)));
  const sets = [0, 1, 2, 3].map((s) => cstr(p.fcmp_print_set(s)));
  note(`modes: ${modes.join(' ')}`);
  const wantedModes = cfg.modes === 'all' ? modes : cfg.modes;
  const all0 = performance.now();
  let renderMs = 0, setupMs = 0, hashMs = 0, compared = 0;

  // One row: { key, m, setup, s }. `group` rows share one context and one engine: the program loops, and at each
  // multiple of its length the context is suspended, the next row's records go in (a reconfigure: a fresh engine,
  // snapped at the row's values) and the context resumes.
  const armRow = async (node, r) => {
    const [quality, budget, look] = SETUP[r.setup];
    if (cfg.via === 'reconfigure') {
      node.port.postMessage(record(r.m, r.s, quality === 1 ? 2 : 1, budget, look, 1));   // another QUALITY first
      node.port.postMessage(record(r.m, r.s, quality, budget, look, 1));
      return 2;
    }
    node.port.postMessage(record(r.m, r.s, quality, budget, look, 1));                     // 'plain': the trap
    return 1;
  };
  const renderGroup = async (group) => {
    const a = performance.now();
    const n = group.length;
    const ctx = new OfflineAudioContext({ numberOfChannels: 2, length: frames * n, sampleRate: 48000 });
    await ctx.audioWorklet.addModule('fcmp-worklet.js');
    const made = await makeNode(ctx, engineBytes);
    const armed = [];
    let sent = await armRow(made.node, group[0]);
    armed.push({ stats: await ask(made.node, 'stats'), sent });
    for (let k = 1; k < n; k += 1) {
      ctx.suspend(k * frames / 48000).then(async () => {
        sent += await armRow(made.node, group[k]);
        armed.push({ stats: await ask(made.node, 'stats'), sent });
        ctx.resume();
      });
    }
    const buffer = ctx.createBuffer(2, frames, 48000);
    buffer.copyToChannel(inL, 0);
    buffer.copyToChannel(inR, 1);
    const source = ctx.createBufferSource();
    source.buffer = buffer;
    source.loop = n > 1;
    source.connect(made.node).connect(ctx.destination);
    source.start();
    const b = performance.now();
    const rendered = await within(120000, 'the offline render', ctx.startRendering());
    const c = performance.now();
    const after = await ask(made.node, 'stats');
    const allL = rendered.getChannelData(0), allR = rendered.getChannelData(1);
    setupMs += b - a; renderMs += c - b;
    for (let k = 0; k < n; k += 1) {
      const r = group[k];
      const d0 = performance.now();
      const gotL = hash(allL.subarray(k * frames, (k + 1) * frames)), gotR = hash(allR.subarray(k * frames, (k + 1) * frames));
      hashMs += performance.now() - d0;
      const name = `${r.key} ${r.setup} ${sets[r.s]}`;
      const want = r.setup === 'std' ? { l: (golden[r.key] || {})[`print.${sets[r.s]}.l.hash`], r: (golden[r.key] || {})[`print.${sets[r.s]}.r.hash`] }
                                     : expect[name] || {};
      const wantLatency = (expect[name] || {}).latency;
      const before = armed[k] || { stats: {} };
      const ran = before.stats.quanta === k * frames / QUANTUM && before.stats.records === before.sent
               && before.stats.refused === 0 && before.stats.latency === wantLatency && after.ok === true
               && after.quanta === n * frames / QUANTUM && after.oddQuanta === 0 && after.inChannels === 2;
      compared += 2;
      const from = r.setup === 'std' ? 'golden' : 'native';
      row(ran && gotL === want.l, `${r.key} ${r.setup} print.${sets[r.s]}.l.hash`, gotL === want.l ? gotL : `${gotL} ${from} ${want.l}`);
      row(ran && gotR === want.r, `${r.key} ${r.setup} print.${sets[r.s]}.r.hash`, gotR === want.r ? gotR : `${gotR} ${from} ${want.r}`);
      if (!ran) note(`${name}: armed ${JSON.stringify(before)} after ${JSON.stringify(after)} want latency ${wantLatency}`);
    }
    if (cfg.timing) note(`${group[0].key} ${group[0].setup} x${n}: setup ${(b - a).toFixed(0)} ms, render ${(c - b).toFixed(0)} ms (${(4000 * n / (c - b)).toFixed(1)}x real time)`);
  };
  const rowsWanted = [];
  for (const key of wantedModes) {
    const m = modes.indexOf(key);
    if (m < 0) { row(false, `${key}`, 'not a registered Mode'); continue; }
    for (const setup of cfg.setups)
      for (let s = 0; s < 4; s += 1)
        if (cfg.sets.includes(sets[s])) rowsWanted.push({ key, m, setup, s });
  }
  const groups = [];
  if (cfg.contexts === 'one') groups.push(rowsWanted);
  else if (cfg.contexts === 'mode') {
    for (const r of rowsWanted) {
      const last = groups[groups.length - 1];
      if (last && last[0].key === r.key && last[0].setup === r.setup) last.push(r); else groups.push([r]);
    }
  } else for (const r of rowsWanted) groups.push([r]);
  note(`${rowsWanted.length} renders in ${groups.length} context(s)`);
  for (const g of groups) {
    await renderGroup(g);
    if (cfg.gc && typeof globalThis.gc === 'function') { globalThis.gc(); await new Promise((r) => setTimeout(r, 20)); }
  }
  const total = performance.now() - all0;
  note(`${compared} rows in ${(total / 1000).toFixed(1)} s: contexts and worklets ${(setupMs / 1000).toFixed(1)} s, renders ${(renderMs / 1000).toFixed(1)} s, hashes ${(hashMs / 1000).toFixed(1)} s`);
  row(compared === cfg.expectRows, 'rows.count', `${compared} compared, ${cfg.expectRows} expected`);
}

main().catch((error) => row(false, 'ran', String((error && error.stack) || error))).then(() => {
  say(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
  document.title = failure === '' ? 'PASS' : `FAIL: ${failure}`;
});
