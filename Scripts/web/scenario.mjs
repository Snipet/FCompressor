// The candidate for the repository's permanent scenario: the SHIPPED site, no hook. Controls are found by Layout.h's
// constants (frozen at FZ4), and every row is judged by what crossed the port (the Params records the editor posted,
// the replies the engine sent) or by the page's own DOM, never by a picture.
import { serve, chrome, sleep, Report, HERE, cleanUp, PID } from './lib.mjs';
import { join } from 'node:path';
const R = new Report();
const server = await serve(`${HERE}/${process.argv[2] || 'site'}`);
const b = await chrome({ width: 1280, height: 800 });
const L = {                                                        // Source/editor/Layout.h, logical px
  threshold: [96, 406],                                            // slotGeom(p, 0): x 40, top 362, w 112; the value line
  presetNext: [540, 30], presetPrev: [240, 30],                    // header::kPresetStrip's chevrons (PresetStrip.h)
  modeNext: [908, 30], modePrev: [668, 30],                        // header::kModeNext, kModePrev
  qualityStd: [498, 70], qualityHq: [531, 70],                     // display::kQualityCells
  lookOff: [462, 96], look5: [501, 96],                            // display::kLookaheadCells
  characteristics: [854, 90], gear: [208, 24], nothing: [300, 240],
};
try {
  const p = await b.page(null);
  await p.metrics(1280, 800, 1);
  await p.go(`${server.base}/`, 1500);
  const ms = await p.start(); await sleep(1000); await p.tap(); await sleep(500);
  const T = async () => { const t = await p.tapRead(); return { n: t.n, v: t.last ? t.last.v : null, snap: t.last ? t.last.snap : null, r: t.reply }; };
  const st0 = await p.status(); const ws0 = await p.stats();
  R.row(st0.ok === 1 && st0.replies > 0 && (st0.flags & 0x38) === 0x30 && ws0.ok && ws0.refused === 0, 'start', `running ${ms} ms after the click; ${st0.replies} replies, flags 0x${st0.flags.toString(16)}, latency ${st0.latency}`);
  let t0 = await T();
  await p.drag(...L.threshold, -60, 0, { steps: 30 }); await sleep(400);
  let t1 = await T();
  R.row(t1.n > t0.n && t1.v[PID.thr] < -18 && Math.abs(t1.r.thrDb - t1.v[PID.thr]) < 0.01 && t1.snap === 0, 'drag.threshold', `${t1.n - t0.n} records, THRESHOLD ${t1.v[PID.thr]}, the engine runs ${t1.r.thrDb}`);
  await p.doubleClick(...L.threshold); await sleep(400);
  let t2 = await T();
  R.row(t2.v[PID.thr] === -18 && Math.abs(t2.r.thrDb + 18) < 0.01, 'doubleclick.resets', `THRESHOLD ${t2.v[PID.thr]}, engine ${t2.r.thrDb}`);
  await p.click(...L.threshold); await p.type('-30'); await p.key('Enter'); await sleep(400);
  let t3 = await T();
  R.row(t3.v[PID.thr] === -30 && Math.abs(t3.r.thrDb + 30) < 0.01, 'typed-value', `THRESHOLD ${t3.v[PID.thr]}, engine ${t3.r.thrDb}`);
  await p.click(...L.nothing); await p.key('z', { modifiers: await p.ev("/mac|iphone|ipad/i.test(navigator.userAgentData ? navigator.userAgentData.platform : navigator.platform)") ? 4 : 2 }); await sleep(400);
  let t4 = await T();
  R.row(t4.v[PID.thr] === -18, 'undo.chord', `THRESHOLD ${t4.v[PID.thr]} after the command key and Z`);
  await p.click(...L.presetNext); await sleep(500);
  let t5 = await T();
  await p.click(...L.presetPrev); await sleep(500);
  let t6 = await T();
  R.row(t5.snap === 1 && t5.v[PID.thr] !== -18 && t6.snap === 1 && t6.v[PID.thr] === -18 && t6.v[PID.ratio] === t4.v[PID.ratio], 'preset.next-previous', `next: a snapped record, THRESHOLD ${t5.v[PID.thr]}; previous: ${t6.v[PID.thr]}`);
  await p.click(...L.modeNext); await sleep(900);
  let t7 = await T();
  await p.click(...L.modePrev); await sleep(900);
  let t8 = await T();
  R.row(t7.v[PID.mode] !== 0 && t7.r.modeSlot === t7.v[PID.mode] && t8.v[PID.mode] === 0 && t8.r.modeSlot === 0 && t8.r.fade === 1, 'mode.next-previous', `next: slot ${t7.v[PID.mode]}, the engine runs ${t7.r.modeSlot}; previous: ${t8.r.modeSlot}`);
  const q0 = (await p.stats()).quanta;
  await p.click(...L.qualityHq); await sleep(700);
  const sHq = await p.status(); const wHq = await p.stats();
  await p.click(...L.look5); await sleep(700);
  const s5 = await p.status();
  await p.click(...L.qualityStd); await p.click(...L.lookOff); await sleep(700);
  const sStd = await p.status(); const wStd = await p.stats();
  R.row(sHq.latency === 61 && wHq.latency === 61 && s5.latency === 301 && sStd.latency === 4 && wStd.refused === 0 && wStd.ok && wStd.quanta > q0 + 375, 'quality-and-lookahead', `latency STD 4 -> HQ ${sHq.latency} -> HQ + 5 MS ${s5.latency} -> STD, OFF ${sStd.latency}; ${wStd.quanta - q0} quanta meanwhile, refused ${wStd.refused}`);
  const tone = join(HERE, 'files', 'scout-tone.wav');
  const data = { items: [], files: [tone], dragOperationsMask: 1 };
  for (const type of ['dragEnter', 'dragOver', 'drop']) await p.s('Input.dispatchDragEvent', { type, x: 400, y: 300, data });
  await sleep(1000);
  const src = await p.ev("document.getElementById('fcmp-source-name').textContent"); const t9 = await T();
  R.row(src === 'SOURCE: SCOUT-TONE.WAV' && Math.abs(t9.r.inPeak[0] + 12) < 0.6, 'dropped-file', `${src}; the engine's input peaks at ${t9.r.inPeak[0].toFixed(1)} dBFS (the file: -12)`);
  await p.ev("(() => { const gl = document.getElementById('fcmp-canvas').getContext('webgl2'); globalThis.__lose = gl.getExtension('WEBGL_lose_context'); __lose.loseContext(); })()"); await sleep(400);
  const sl = await p.status();
  await p.ev('__lose.restoreContext()'); await sleep(700);
  const sr0 = await p.status(); await sleep(500); const sr1 = await p.status();
  R.row(sl.ok === 0 && sl.error === '' && sl.lost > 0 && sr1.ok === 1 && sr1.frames > sr0.frames && sr1.lost === sr0.lost, 'context-loss', `lost: ok ${sl.ok}, ${sl.lost} frames not drawn; restored: ok ${sr1.ok}, ${sr1.frames - sr0.frames} frames in 0.5 s`);
  const end = await p.stats(); const se = await p.status();
  R.row(p.consoleLines.length === 0 && end.ok && end.refused === 0 && se.refused === 0 && se.posted === end.records, 'no-error', `console: ${p.consoleLines.length} lines; ${se.posted} records posted, ${end.records} taken, ${end.refused} refused`);
} catch (e) { R.row(false, 'driver', String(e && e.stack || e)); }
finally { b.kill(); server.kill(); cleanUp(); }
const fails = R.rows.filter((r) => r.ok === false).length;
console.log(`\n${fails} FAIL of ${R.rows.length}`);
process.exit(fails ? 1 : 0);
