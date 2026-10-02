// SCOUT SCRATCH (lead phase, hand checks): a DevTools driver with real input events for the FCompressor page.
// node only, no dependency. Chrome is ALWAYS headless, muted, ANGLE on Metal, autoplay allowed, its own profile.
import { spawn, execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';

export const HERE = import.meta.dirname;
export const PNG = join(HERE, 'png');
mkdirSync(PNG, { recursive: true });
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

const lineFrom = (child, pattern, ms = 20000) => new Promise((done) => {
  let seen = '';
  const look = (chunk) => { seen += chunk; const m = seen.match(pattern); if (m) done(m); };
  child.stdout.on('data', look); child.stderr.on('data', look);
  child.on('exit', () => done(null)); setTimeout(() => done(null), ms);
});

const children = [];
const profiles = [];
export function cleanUp() {
  for (const c of children) { try { c.kill('SIGKILL'); } catch {} }
  for (const p of profiles) { try { rmSync(p, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 }); } catch {} }
}
process.on('exit', cleanUp);
process.on('SIGINT', () => { cleanUp(); process.exit(2); });
process.on('SIGTERM', () => { cleanUp(); process.exit(2); });
process.on('uncaughtException', (e) => { console.log('DRIVER ERROR ' + (e && e.stack || e)); cleanUp(); process.exit(2); });

export async function serve(dir) {
  const server = spawn('python3', ['-u', '-m', 'http.server', '0', '--bind', '127.0.0.1', '--directory', dir],
                       { stdio: ['ignore', 'pipe', 'pipe'] });
  children.push(server);
  const m = await lineFrom(server, /Serving HTTP on 127\.0\.0\.1 port (\d+)/);
  if (!m) throw new Error('no server');
  return { base: `http://127.0.0.1:${m[1]}`, kill: () => { try { server.kill('SIGKILL'); } catch {} } };
}

export async function chrome({ width = 1280, height = 800, extra = [], profile = '' } = {}) {
  if (profile === '') { profile = mkdtempSync(join(HERE, 'profile-')); profiles.push(profile); }
  const flags = ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`, '--no-first-run',
                 '--no-default-browser-check', '--disable-extensions', '--mute-audio',
                 ...(process.env.SCOUT_GPU ? process.env.SCOUT_GPU.split(' ') : ['--use-angle=metal']),
                 ...(process.env.SCOUT_FLAGS ? process.env.SCOUT_FLAGS.split(' ') : []),
                 '--autoplay-policy=no-user-gesture-required', `--window-size=${width},${height}`, ...extra, 'about:blank'];
  const browser = spawn(CHROME, flags, { stdio: ['ignore', 'pipe', 'pipe'] });
  children.push(browser);
  const m = await lineFrom(browser, /DevTools listening on (ws:\/\/[^\s]+)/);
  if (!m) { browser.kill('SIGKILL'); throw new Error('no chrome'); }
  const socket = new WebSocket(m[1]);
  await new Promise((open, bad) => { socket.addEventListener('open', open, { once: true });
                                     socket.addEventListener('error', bad, { once: true }); });
  let nextId = 0;
  const waiting = new Map();
  const listeners = [];
  socket.addEventListener('message', (event) => {
    const msg = JSON.parse(event.data);
    if (msg.id !== undefined && waiting.has(msg.id)) {
      const { answer, reject } = waiting.get(msg.id);
      waiting.delete(msg.id);
      if (msg.error) reject(new Error(msg.error.message)); else answer(msg.result);
    } else if (msg.method) for (const l of listeners) l(msg);
  });
  const send = (method, params = {}, sessionId = undefined) => new Promise((answer, reject) => {
    const id = ++nextId;
    const timer = setTimeout(() => { if (waiting.has(id)) { waiting.delete(id); reject(new Error(`${method} unanswered`)); } }, 60000);
    waiting.set(id, { answer: (v) => { clearTimeout(timer); answer(v); }, reject: (e) => { clearTimeout(timer); reject(e); } });
    socket.send(JSON.stringify({ id, method, params, sessionId }));
  });

  async function page(url, { newWindow = false, w = width, h = height } = {}) {
    const { targetId } = await send('Target.createTarget', newWindow ? { url: 'about:blank', width: w, height: h, newWindow } : { url: 'about:blank' });
    const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
    const consoleLines = [];
    listeners.push((msg) => {
      if (msg.sessionId !== sessionId) return;
      if (msg.method === 'Runtime.consoleAPICalled')
        consoleLines.push(`console.${msg.params.type}: ${msg.params.args.map((a) => a.value ?? a.description).join(' ')}`);
      else if (msg.method === 'Runtime.exceptionThrown')
        consoleLines.push(`EXCEPTION: ${msg.params.exceptionDetails.text} ${(msg.params.exceptionDetails.exception || {}).description || ''}`);
      else if (msg.method === 'Log.entryAdded')
        consoleLines.push(`log.${msg.params.entry.level}: ${msg.params.entry.text} ${msg.params.entry.url || ''}`);
    });
    const s = (method, params = {}) => send(method, params, sessionId);
    await s('Runtime.enable'); await s('Log.enable'); await s('Page.enable'); await s('DOM.enable');
    const ev = async (expression) => {
      let r; try { r = await s('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }); } catch (e) { throw new Error(`${e.message} [${String(expression).slice(0, 90)}] visibility unknown`); }
      if (r.exceptionDetails) throw new Error(`evaluate: ${r.exceptionDetails.text} ${(r.exceptionDetails.exception || {}).description || ''}`);
      return r.result.value;
    };
    const P = { s, ev, consoleLines, targetId, sessionId };

    // ---- the device ----
    P.metrics = (w2, h2, dsf) => s('Emulation.setDeviceMetricsOverride', { width: w2, height: h2, deviceScaleFactor: dsf, mobile: false });
    P.go = async (u, settle = 1200) => { await s('Page.navigate', { url: u }); await sleep(settle); };
    P.reload = async (settle = 1500) => { await s('Page.reload', {}); await sleep(settle); };

    // ---- the page's own state ----
    P.status = async () => JSON.parse(await ev('Module.fcmpStatus()'));
    P.stats = async () => JSON.parse(await ev('fcmpPage.stats().then(JSON.stringify)'));
    P.a11y = async () => JSON.parse(await ev("typeof Module.fcmpA11y === 'function' ? Module.fcmpA11y() : 'null'"));
    P.canvas = async () => JSON.parse(await ev("JSON.stringify((()=>{const r=document.getElementById('fcmp-canvas').getBoundingClientRect(); return {x:r.left,y:r.top,w:r.width,h:r.height, bw: document.getElementById('fcmp-canvas').width, bh: document.getElementById('fcmp-canvas').height, dpr: devicePixelRatio, iw: innerWidth, ih: innerHeight, sx: scrollX, sy: scrollY};})())"));
    // Panel logical px -> client px, through the canvas box as it is NOW (the zoom may have changed)
    P.at = async (x, y) => { const c = await P.canvas(); return [c.x + x * c.w / 960, c.y + y * c.h / 640]; };

    // ---- real input ----
    let buttonsDown = 0;
    const mouse = (type, x, y, more = {}) => s('Input.dispatchMouseEvent', { type, x, y, button: 'none', buttons: buttonsDown, pointerType: 'mouse', ...more });
    P.moveClient = (x, y) => mouse('mouseMoved', x, y);
    P.move = async (x, y) => { const [cx, cy] = await P.at(x, y); await mouse('mouseMoved', cx, cy); };
    P.pressClient = async (x, y, { button = 'left', clickCount = 1, modifiers = 0 } = {}) => {
      await mouse('mouseMoved', x, y, { modifiers }); await sleep(30);
      buttonsDown = button === 'right' ? 2 : 1;
      await mouse('mousePressed', x, y, { button, buttons: buttonsDown, clickCount, modifiers });
    };
    P.releaseClient = async (x, y, { button = 'left', clickCount = 1, modifiers = 0 } = {}) => {
      buttonsDown = 0;
      await mouse('mouseReleased', x, y, { button, buttons: 0, clickCount, modifiers });
    };
    P.clickClient = async (x, y, o = {}) => { await P.pressClient(x, y, o); await sleep(40); await P.releaseClient(x, y, o); };
    P.click = async (x, y, o = {}) => { const [cx, cy] = await P.at(x, y); await P.clickClient(cx, cy, o); await sleep(o.settle ?? 250); };
    P.rightClick = async (x, y, o = {}) => P.click(x, y, { ...o, button: 'right' });
    P.doubleClick = async (x, y) => {
      const [cx, cy] = await P.at(x, y);
      await P.pressClient(cx, cy, { clickCount: 1 }); await sleep(30); await P.releaseClient(cx, cy, { clickCount: 1 });
      await sleep(60);
      await P.pressClient(cx, cy, { clickCount: 2 }); await sleep(30); await P.releaseClient(cx, cy, { clickCount: 2 });
      await sleep(300);
    };
    // a drag in Panel px from (x, y) by (dx, dy), in `steps` moves 16 ms apart; the button is released at the end
    P.drag = async (x, y, dx, dy, { steps = 20, hold = null, modifiers = 0 } = {}) => {
      const c = await P.canvas(); const k = c.w / 960;
      const cx = c.x + x * k, cy = c.y + y * k;
      await P.pressClient(cx, cy, { modifiers });
      for (let i = 1; i <= steps; i += 1) {
        await mouse('mouseMoved', cx + dx * k * i / steps, cy + dy * k * i / steps, { button: 'left', buttons: 1, modifiers });
        await sleep(16);
      }
      if (hold) await hold();
      await P.releaseClient(cx + dx * k, cy + dy * k, { modifiers });
      await sleep(250);
    };
    P.wheel = async (x, y, deltaY, { deltaX = 0, modifiers = 0 } = {}) => {
      const [cx, cy] = await P.at(x, y);
      await mouse('mouseMoved', cx, cy);
      await s('Input.dispatchMouseEvent', { type: 'mouseWheel', x: cx, y: cy, deltaX, deltaY, modifiers, pointerType: 'mouse' });
      await sleep(120);
    };
    const VK = { Enter: 13, Escape: 27, Tab: 9, Backspace: 8, Delete: 46, ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37, ArrowRight: 39,
                 Home: 36, End: 35, PageUp: 33, PageDown: 34, ' ': 32 };
    // modifiers: Alt 1, Ctrl 2, Meta 4, Shift 8.  NEVER pass nativeVirtualKeyCode: on macOS headless Chrome the key then
    // repeats without end (8,500 keydowns in a second on a plain page) and starves the page's timers and messages.
    P.key = async (key, { modifiers = 0, settle = 120 } = {}) => {
      const named = key.length > 1;
      const code = named ? key : (/[a-z]/i.test(key) ? `Key${key.toUpperCase()}` : /[0-9]/.test(key) ? `Digit${key}` : key === ' ' ? 'Space' : '');
      const vk = VK[key] ?? key.toUpperCase().charCodeAt(0);
      const text = named ? (key === 'Enter' ? '\r' : undefined) : ((modifiers & 6) ? undefined : key);
      await s('Input.dispatchKeyEvent', { type: text !== undefined ? 'keyDown' : 'rawKeyDown', key, code, windowsVirtualKeyCode: vk, modifiers, text, unmodifiedText: text });
      await s('Input.dispatchKeyEvent', { type: 'keyUp', key, code, windowsVirtualKeyCode: vk, modifiers });
      await sleep(settle);
    };
    P.type = async (textToType) => { for (const ch of textToType) await P.key(ch, { settle: 40 }); await sleep(120); };

    // ---- a control by its accessibility title (the hook) ----
    P.find = async (title, { parent = null, role = null, nth = 0 } = {}) => {
      const a = await P.a11y();
      if (!a) throw new Error('no Module.fcmpA11y');
      let par = null;
      if (parent !== null) { par = a.items.find((i) => i.title === parent); if (!par) throw new Error(`no a11y item "${parent}"`); }
      const hits = a.items.filter((i) => (title instanceof RegExp ? title.test(i.title) : i.title === title) && (par === null || i.parent === par.id) && (role === null || i.role === role));
      return hits[nth] || null;
    };
    P.need = async (title, o = {}) => { const it = await P.find(title, o); if (!it) throw new Error(`no a11y item "${title}"${o.parent ? ` in "${o.parent}"` : ''}`); return it; };
    P.clickItem = async (title, o = {}) => { const it = await P.need(title, o); await P.click(it.x + it.w / 2, it.y + it.h / 2, o); return it; };
    P.value = async (title, o = {}) => { const it = await P.find(title, o); return it ? it.value : null; };
    P.footer = () => P.value('Footer');

    // ---- FunkGui's DOM menu (WebServices): its rows, and a real click on one ----
    P.menu = async () => JSON.parse(await ev(`JSON.stringify((() => { const m = document.querySelector('[data-funkgui-menu]'); if (!m) return null;
      const r = m.getBoundingClientRect();
      return { serial: m.getAttribute('data-funkgui-menu'), box: [r.left, r.top, r.width, r.height], focused: m.contains(document.activeElement),
               items: Array.from(m.children).map((el) => { const b = el.getBoundingClientRect(); return { role: el.getAttribute('role'), text: el.textContent, id: el.getAttribute('data-id'), disabled: el.getAttribute('aria-disabled') === 'true', checked: el.getAttribute('aria-checked') === 'true', x: b.left + b.width / 2, y: b.top + b.height / 2 }; }) }; })())`));
    P.menuChoose = async (text) => { const m = await P.menu(); if (!m) throw new Error('no menu is open'); const it = m.items.find((i) => i.text === text || (text instanceof RegExp && text.test(i.text))); if (!it) throw new Error(`no menu item ${text}: ${m.items.map((i) => i.text).join(' | ')}`); await sleep(300); await P.clickClient(it.x, it.y); await sleep(300); return it; };

    // ---- pictures ----
    P.shot = async (name, { canvasOnly = true } = {}) => {
      let clip;
      if (canvasOnly) { const c = await P.canvas(); clip = { x: c.x + c.sx, y: c.y + c.sy, width: c.w, height: c.h, scale: 1 }; }
      const r = await s('Page.captureScreenshot', { format: 'png', clip, captureBeyondViewport: !!clip });
      const file = join(PNG, name);
      writeFileSync(file, Buffer.from(r.data, 'base64'));
      return file;
    };

    // ---- START, and the taps on the port ----
    P.start = async () => {
      const at = JSON.parse(await ev("JSON.stringify((()=>{const r=document.getElementById('fcmp-start').getBoundingClientRect(); return [r.left+r.width/2, r.top+r.height/2];})())"));
      const t0 = Date.now();
      await P.clickClient(at[0], at[1]);
      while (await ev('fcmpPage.state()') !== 'running' && Date.now() - t0 < 20000) await sleep(25);
      return Date.now() - t0;
    };
    // Every Params record the editor posts (30 plain values + snap), and the newest reply's head, kept in the page.
    P.tap = () => ev(`(() => {
      const port = fcmpPage.node().port;
      const T = globalThis.__tap = { params: [], kinds: {}, reply: null, replies: 0 };
      const orig = port.postMessage.bind(port);
      port.postMessage = (m, t) => {
        if (m instanceof ArrayBuffer) {
          const kind = new Uint16Array(m, 6, 1)[0];
          T.kinds[kind] = (T.kinds[kind] || 0) + 1;
          if (kind === 1) T.params.push({ t: performance.now(), v: Array.from(new Float32Array(m.slice(16, 136))), snap: new Uint32Array(m.slice(136, 140))[0] });
        }
        return orig(m, t);
      };
      port.addEventListener('message', (e) => {
        const b = e.data;
        if (!(b instanceof ArrayBuffer) || b.byteLength < 320) return;
        const u = new Uint32Array(b, 0, 80), f = new Float32Array(b, 0, 80), h = new Uint16Array(b, 0, 40);
        T.replies += 1;
        T.reply = { flags: u[4], latency: u[5], columns: u[6], publish: u[8], modeSlot: h[18], fadeFrom: h[19], uiFlags: u[10], rate: f[11],
                    frameLatency: u[12], fade: f[13], bypass: f[14], inPeak: [f[16], f[17]], outPeak: [f[20], f[21]],
                    gr: [f[32], f[33]], blockMaxGr: [f[34], f[35]], thrDb: f[45], slope: f[46] };
      });
      return true; })()`);
    P.tapRead = async (clear = false) => JSON.parse(await ev(`JSON.stringify((() => { const T = globalThis.__tap; const r = { n: T.params.length, last: T.params[T.params.length - 1] || null, kinds: T.kinds, reply: T.reply, replies: T.replies }; ${clear ? 'T.params.length = 0;' : ''} return r; })())`));
    if (url) await P.go(url);
    return P;
  }
  const kill = () => { try { socket.close(); } catch {} try { browser.kill('SIGKILL'); } catch {} };
  return { send, page, kill, profile, pid: browser.pid };
}

// The system pasteboard's change count (no content is read).
export function pasteboardCount() {
  try {
    return Number(execFileSync('osascript', ['-l', 'JavaScript', '-e', 'ObjC.import("AppKit"); $.NSPasteboard.generalPasteboard.changeCount'], { encoding: 'utf8' }).trim());
  } catch { return -1; }
}

export const PID = { thr: 0, ratio: 1, knee: 2, range: 3, atk: 4, rel: 5, tmode: 6, hold: 7, look: 8, det: 9, schpf: 10, sce: 11, link: 12,
  stmode: 13, voice: 14, drive: 15, makeup: 16, automu: 17, mix: 18, s2thr: 19, s2atk: 20, s2rel: 21, mode: 22, extkey: 23, listen: 24,
  delta: 25, bypass: 26, quality: 27, labudget: 28, output: 29 };

export class Report {
  constructor() { this.rows = []; }
  row(ok, id, detail) { this.rows.push({ ok, id, detail }); console.log(`${ok === null ? 'NOTE' : ok ? 'PASS' : 'FAIL'}  ${id}: ${typeof detail === 'string' ? detail : JSON.stringify(detail)}`); return ok; }
  note(id, detail) { return this.row(null, id, detail); }
}
