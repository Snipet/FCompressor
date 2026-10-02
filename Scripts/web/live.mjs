#!/usr/bin/env node
// scout-l prototype of Scripts/web/live.mjs: the runner behind Scripts/web-live.sh.
//   node live.mjs --dir <site> --out <dir> [--golden <ui.geometry.txt>] [--views a,b] [--themes 0,1] [--selftest]
//                 [--serve [--port N]] [--chrome <path>] [--timeout <s>]
// One node static server on 127.0.0.1 (no python), one headless Chrome (always --mute-audio), one tab per page.
// Exit: 0 every row passed; 1 a row failed; 2 it could not run.
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { extname, join, normalize, resolve, sep } from 'node:path';

const args = process.argv.slice(2);
const opt = (name, otherwise) => {
  const i = args.indexOf(name);
  return i < 0 ? otherwise : args[i + 1];
};
const has = (name) => args.includes(name);
const dir = resolve(opt('--dir', ''));
const out = resolve(opt('--out', 'web-live'));
const golden = opt('--golden', '');
const views = opt('--views', 'panel,chars.sidechain,chars.colour,modebrowser,presetbrowser,settings').split(',');
const themes = opt('--themes', '0').split(',');
const timeoutS = Number(opt('--timeout', '120'));
const chrome = opt('--chrome', process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');
const DT = '0.0166666675';

const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript',
               '.wasm': 'application/wasm', '.css': 'text/css', '.txt': 'text/plain; charset=utf-8' };
function serve(root, port) {
  const server = createServer((req, res) => {
    const path = normalize(decodeURIComponent(new URL(req.url, 'http://x').pathname));
    let file = join(root, path);
    if (!file.startsWith(root + sep) && file !== root) { res.writeHead(403).end(); return; }
    if (existsSync(file) && statSync(file).isDirectory()) file = join(file, 'index.html');
    if (!existsSync(file)) { res.writeHead(404).end('not found'); return; }
    res.writeHead(200, { 'Content-Type': MIME[extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    res.end(readFileSync(file));
  });
  return new Promise((ready, bad) => {
    server.on('error', bad);
    server.listen(port, '127.0.0.1', () => ready(server));
  });
}

let exitCode = 2;
const children = [];
let profile = '';
let server = null;
function end(code) {
  for (const c of children) { try { c.kill('SIGKILL'); } catch { /* gone */ } }
  if (server) server.close();
  if (profile) { try { rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 }); } catch { /* */ } }
  process.exit(code);
}
process.on('SIGINT', () => end(2));
process.on('SIGTERM', () => end(2));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function main() {
  if (!existsSync(join(dir, 'index.html'))) { console.error(`web-live: ${dir} has no index.html`); end(2); }
  server = await serve(dir, Number(opt('--port', '0')));
  const base = `http://127.0.0.1:${server.address().port}/`;
  if (has('--serve')) {
    console.log(`web-live: serving ${dir} at ${base} (Ctrl-C to stop); the self-test is ${base}?selftest=1`);
    return;                                                // the server keeps the process alive
  }
  mkdirSync(out, { recursive: true });
  profile = mkdtempSync(join(tmpdir(), 'fcmp-web-live-'));
  const flags = ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`, '--no-first-run',
                 '--no-default-browser-check', '--disable-extensions', '--mute-audio',
                 process.platform === 'darwin' ? '--use-angle=metal' : '--use-angle=swiftshader',
                 ...(process.platform === 'darwin' ? [] : ['--enable-unsafe-swiftshader']),
                 '--autoplay-policy=no-user-gesture-required', '--window-size=1280,1000', 'about:blank'];
  const browser = spawn(chrome, flags, { stdio: ['ignore', 'pipe', 'pipe'] });
  children.push(browser);
  const ws = await new Promise((done) => {
    let seen = '';
    browser.stderr.on('data', (c) => { seen += c; const m = seen.match(/DevTools listening on (ws:\/\/\S+)/); if (m) done(m[1]); });
    browser.on('exit', () => done(null));
    setTimeout(() => done(null), 30000);
  });
  if (!ws) { console.error('web-live: Chrome did not start with a DevTools port'); end(2); }
  const socket = new WebSocket(ws);
  await new Promise((open, bad) => { socket.addEventListener('open', open, { once: true }); socket.addEventListener('error', bad, { once: true }); });
  let nextId = 0;
  const waiting = new Map();
  socket.addEventListener('message', (event) => {
    const m = JSON.parse(event.data);
    if (m.id !== undefined && waiting.has(m.id)) {
      const w = waiting.get(m.id);
      waiting.delete(m.id);
      if (m.error) w.reject(new Error(m.error.message)); else w.answer(m.result);
    }
  });
  const send = (method, params = {}, sessionId = undefined) => new Promise((answer, reject) => {
    const id = ++nextId;
    waiting.set(id, { answer, reject });
    socket.send(JSON.stringify({ id, method, params, sessionId }));
    setTimeout(() => { if (waiting.delete(id)) reject(new Error(`${method} was not answered`)); }, timeoutS * 1000);
  });
  const version = await send('Browser.getVersion');
  const summary = [];
  const say = (line) => { console.log(line); summary.push(line); };
  say(`web-live: ${dir} at ${base} in ${version.product}`);
  try {
    const info = await send('SystemInfo.getInfo');
    const aux = info.gpu.auxAttributes || {};
    say(`NOTE     gpu: ${info.gpu.devices.map((d) => d.deviceString || d.vendorString).join(', ')}; ${aux.glRenderer || ''}; ${info.modelName || ''}`);
  } catch (e) { say(`NOTE     gpu: unknown (${e.message})`); }

  // A page: open, wait until `until` (an expression) is truthy, then evaluate each of `reads`.
  async function page(url, until, reads, shot) {
    const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
    await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 1000, deviceScaleFactor: 2, mobile: false }, sessionId);
    await send('Page.enable', {}, sessionId);
    const t0 = Date.now();
    await send('Page.navigate', { url }, sessionId);
    const evaluate = async (expression) => {
      const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }, sessionId);
      if (r.exceptionDetails) throw new Error(`${expression}: ${r.exceptionDetails.exception?.description || r.exceptionDetails.text}`);
      return r.result.value;
    };
    const deadline = Date.now() + timeoutS * 1000;
    let ready = false;
    while (Date.now() < deadline) {
      ready = await evaluate(until).catch(() => false);
      if (ready) break;
      await sleep(50);
    }
    const values = [];
    if (ready) for (const r of reads) values.push(await evaluate(r));
    if (shot) {
      // The editor alone: the page's overlay and its `waiting` veil are taken away, then the canvas's box is the clip.
      const box = await evaluate(`(async () => {
        document.getElementById('fcmp-overlay').style.display = 'none';
        document.getElementById('fcmp-stage').classList.remove('waiting');
        await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
        const b = document.getElementById('fcmp-canvas').getBoundingClientRect();
        return [b.left + scrollX, b.top + scrollY, b.width, b.height];
      })()`);
      const png = await send('Page.captureScreenshot', { format: 'png', clip: { x: box[0], y: box[1], width: box[2], height: box[3], scale: 1 } }, sessionId);
      writeFileSync(shot, Buffer.from(png.data, 'base64'));
    }
    await send('Target.closeTarget', { targetId });
    return { ready, values, ms: Date.now() - t0 };
  }

  // The expected rows: <view>.dpi2.<key> of ui.geometry's golden (the values the node probe passes against).
  const want = new Map();
  if (golden) {
    for (const line of readFileSync(golden, 'utf8').split('\n')) {
      const [key, value, tol] = line.split('\t');
      if (key && !key.startsWith('#')) want.set(key, { value, tol });
    }
  }

  const expectDir = opt('--expect', '');
  const wantFor = (view) => {
    if (!expectDir) return want;
    const m = new Map();
    for (const l of readFileSync(join(expectDir, `${view}.node.fp`), 'utf8').trim().split('\n'))
      m.set(`${view}.dpi2.${l.slice(0, l.indexOf(' '))}`, { value: l.slice(l.indexOf(' ') + 1), tol: 'exact' });
    return m;
  };
  let failed = 0, total = 0;
  for (const view of views) {
    for (const theme of themes) {
      total += 1;
      const name = themes.length > 1 ? `${view}.theme${theme}` : view;
      const url = `${base}index.html?view=${view}&theme=${theme}&` + opt('--pins', `zoom=100&scale=2&dt=${DT}&nohint=1&nolive=1`);
      const r = await page(url, "typeof Module === 'object' && typeof Module.fcmpFrame === 'function'",
                           ['Module.fcmpFrame()', "typeof Module.fcmpDump === 'function' ? Module.fcmpDump() : ''", 'Module.fcmpSelftest()', 'Module.fcmpStatus()', '`dpr ${devicePixelRatio} inner ${innerWidth}x${innerHeight} hidden ${document.hidden} canvas ${document.getElementById("fcmp-canvas").width}x${document.getElementById("fcmp-canvas").height}`'],
                           join(out, `${name}.png`));
      if (!r.ready) { say(`FAIL     ${name}: the editor did not come up in ${timeoutS} s`); failed += 1; continue; }
      const [fp, dump, self, status, facts] = r.values;
      if (has('--facts')) say(`NOTE     ${name}: ${facts}; ${status}`);
      writeFileSync(join(out, `${name}.live.fp`), fp);
      writeFileSync(join(out, `${name}.live.dump`), dump.split('\n').slice(1).join('\n'));
      const rows = new Map(fp.trim().split('\n').map((l) => [l.slice(0, l.indexOf(' ')), l.slice(l.indexOf(' ') + 1)]));
      const hooks = rows.get('hooks');
      const pixels = JSON.parse(self).pixels;
      const differ = [];
      let compared = 0;
      for (const [key, value] of rows) {
        if (key === 'hooks' || key === 'live' || key === 'view_w' || key === 'view_h' || key === 'glyphs_missing') continue;
        const w = wantFor(view).get(`${view}.dpi2.${key}`);
        if (!w) { differ.push(`${key}: no golden row`); continue; }
        compared += 1;
        const equal = w.tol.startsWith('abs:') ? Math.abs(Number(value) - Number(w.value)) <= Number(w.tol.slice(4))
                                               : value === w.value;
        if (!equal) differ.push(`${key}: live ${value}, node ${w.value}`);
      }
      for (const key of wantFor(view).keys()) {
        if (/\.(live|view_w|view_h|glyphs_missing)$/.test(key)) continue;
        const prefix = `${view}.dpi2.`;
        if (key.startsWith(prefix) && !rows.has(key.slice(prefix.length))) differ.push(`${key.slice(prefix.length)}: missing live`);
      }
      const hooked = has('--loose') || /^dpi 2 clock fixed theme \d dt 0\.0166666675 settle \d+ drawn 1 idle 1$/.test(hooks);
      const ok = hooked && differ.length === 0 && compared > 0;
      if (!ok) failed += 1;
      say(`${ok ? 'EQUAL   ' : hooked ? 'DIFFERS ' : 'FAIL    '} ${name}: geometry ${rows.get('geometry')} text ${rows.get('text')} `
          + `(${compared} rows; ${hooks}; pixels largest ${pixels.largest} over2 ${pixels.over2} of ${pixels.samples}; ${r.ms} ms)`);
      for (const d of differ.slice(0, 12)) say(`         ${d}`);
    }
  }
  if (has('--selftest')) {
    const r = await page(`${base}?selftest=1`, "document.title === 'PASS' || document.title.startsWith('FAIL')",
                         ['document.title', "document.getElementById('funkgui-log').textContent"], join(out, 'selftest.png'));
    total += 1;
    const [title, log] = r.ready ? r.values : ['no verdict', ''];
    writeFileSync(join(out, 'selftest.log'), log);
    for (const line of log.trim().split('\n')) say(`  ${line}`);
    if (title !== 'PASS') failed += 1;
    say(`${title === 'PASS' ? 'PASS    ' : 'FAIL    '} selftest: ${title} (${r.ms} ms)`);
  }
  say(`web-live: ${total - failed}/${total} passed (results in ${out})`);
  writeFileSync(join(out, 'summary.txt'), summary.join('\n') + '\n');
  try { await Promise.race([send('Browser.close'), sleep(1000)]); } catch { /* killed below */ }
  exitCode = failed === 0 ? 0 : 1;
  end(exitCode);
}
main().catch((e) => { console.error(`web-live: ${e.message}`); end(2); });
