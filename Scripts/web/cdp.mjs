// Scripts/web/cdp.mjs: the one library for driving headless Chrome (ADR-93, the web lead phase). Scripts/web/live.mjs,
// the runner of the browser gate, and Scripts/web/scenario.mjs, the scripted user, are written on it. Node only, no
// dependency; node 22 or later (checked below).
//
//   serve(site, { live, expect })   the gate's static server on 127.0.0.1, a free port: / is the site (exactly the
//                                   shipped files), /live/ the live directory, /expect/ the expectation directory, so
//                                   a live page reaches the shipped files as ../<name>. GET and HEAD, application/wasm,
//                                   no caching, nothing above a root in any spelling.
//   findChrome(path)                the browser: the path given, else $CHROME, the macOS application, a name on PATH.
//   chrome({ ... })                 one headless Chrome: a throwaway profile, ALWAYS --mute-audio (it plays through
//                                   the machine's speakers otherwise), the GPU flag by platform (ANGLE on Metal on
//                                   macOS, SwiftShader elsewhere), nothing fetched in the background. DevTools goes
//                                   over a pipe, not a port: a Chrome whose driver is gone ends by itself.
//   browser.page(url)               a tab: evaluate, real input events (mouse, keys, wheel), a screenshot, the demo
//                                   page's own state and the taps on the worklet's port.
//   browser.renderer()              the WebGL renderer's name; browser.audioRuns(): whether an AudioContext renders
//                                   (NULL_SINK is the flag for a machine where none does).
//   Report, PID, sleep, pasteboardCount, HERE, PNG: what a scenario needs besides.
//
// Every Chrome and every server started here is stopped on every way out of the process: its end, SIGINT, SIGTERM,
// SIGHUP and an uncaught error (exit 2 for those four). A Chrome is only ever killed by its own process id, never by
// name, and only a profile made here is removed.
import { execFileSync, spawn } from 'node:child_process';
import { accessSync, constants, mkdirSync, mkdtempSync, readFileSync, realpathSync, rmSync, statSync,
         writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { delimiter, dirname, extname, isAbsolute, join, resolve, sep } from 'node:path';

export const NODE_MAJOR = 22;
if (Number(process.versions.node.split('.')[0]) < NODE_MAJOR) {
  console.error(`cdp.mjs: node ${NODE_MAJOR} or later is needed (this is ${process.version}: ${process.execPath})`);
  process.exit(2);
}

export const HERE = import.meta.dirname;
// Where page.shot(<a relative name>) writes unless chrome() or page() is given `png`. Made when the first picture is.
export const PNG = join(HERE, 'png');
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// `promise`'s value, or LATE when it has none after `ms` (the timer does not outlive the answer).
const LATE = Symbol('late');
const within = (ms, promise) => {
  let timer = 0;
  const late = new Promise((r) => { timer = setTimeout(() => r(LATE), ms); });
  return Promise.race([promise, late]).finally(() => clearTimeout(timer));
};

// ---- what is stopped on the way out ---------------------------------------------------------------------------------
const children = [];
const profiles = [];
const servers = [];
export function cleanUp() {
  for (const c of children) { try { c.kill('SIGKILL'); } catch { /* gone */ } }
  for (const s of servers) { try { s.close(); s.closeAllConnections(); } catch { /* closed */ } }
  for (const p of profiles) {
    try { rmSync(p, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 }); } catch { /* left behind */ }
  }
  children.length = 0;
  servers.length = 0;
  profiles.length = 0;
}
process.on('exit', cleanUp);
process.on('SIGINT', () => { cleanUp(); process.exit(2); });
process.on('SIGTERM', () => { cleanUp(); process.exit(2); });
process.on('SIGHUP', () => { cleanUp(); process.exit(2); });   // the terminal went away
process.on('uncaughtException', (e) => {
  console.log('DRIVER ERROR ' + ((e && e.stack) || e));
  cleanUp();
  process.exit(2);
});

// ---- the server -----------------------------------------------------------------------------------------------------
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
                '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
                '.wasm': 'application/wasm', '.json': 'application/json', '.txt': 'text/plain; charset=utf-8',
                '.fp': 'text/plain; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png' };

// The file a request path names, as { file } or { status }. The path is decoded once and taken literally: a `..`
// segment in any spelling (plain, %2e%2e, ..%2f, behind a prefix) is refused, never resolved, and what a link inside
// a root points to must be inside that root too.
function requested(site, roots, url) {
  let path = '';
  try {
    path = decodeURIComponent(String(url).split(/[?#]/, 1)[0]);
  } catch {
    return { status: 400 };
  }
  if (!path.startsWith('/') || path.includes('\0') || path.includes('\\')) return { status: 400 };
  if (path.split('/').includes('..')) return { status: 403 };
  let root = site;
  for (const [prefix, dir] of roots) {
    if (!path.startsWith(prefix)) continue;
    root = dir;
    path = path.slice(prefix.length - 1);
    break;
  }
  let file = join(root, path);
  if (file !== root && !file.startsWith(root + sep)) return { status: 403 };
  try {
    if (statSync(file).isDirectory()) file = join(file, 'index.html');
    if (!statSync(file).isFile()) return { status: 404 };
    const real = realpathSync(file);
    const realRoot = realpathSync(root);
    if (real !== realRoot && !real.startsWith(realRoot + sep)) return { status: 403 };
  } catch {
    return { status: 404 };
  }
  return { file };
}

// Serves `dir` at /, and with `live` and `expect` those directories at /live/ and /expect/. Answers { base (no slash
// at its end), port, misses (every request not answered 200, as "<status> <method> <url>"), kill() }.
export async function serve(dir, { live = '', expect = '', port = 0 } = {}) {
  const site = resolve(dir);
  const roots = [['/live/', live], ['/expect/', expect]].filter(([, d]) => d !== '').map(([p, d]) => [p, resolve(d)]);
  const misses = [];
  const server = createServer((request, response) => {
    const refuse = (status) => {
      misses.push(`${status} ${request.method} ${request.url}`);
      response.writeHead(status, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-store' });
      response.end(request.method === 'HEAD' ? undefined : `${status}\n`);
    };
    if (request.method !== 'GET' && request.method !== 'HEAD') return refuse(405);
    const found = requested(site, roots, request.url);
    if (!found.file) return refuse(found.status);
    let bytes = null;
    try {
      bytes = readFileSync(found.file);
    } catch {
      return refuse(404);
    }
    response.writeHead(200, { 'Content-Type': TYPES[extname(found.file)] || 'application/octet-stream',
                              'Content-Length': bytes.length, 'Cache-Control': 'no-store' });
    return response.end(request.method === 'HEAD' ? undefined : bytes);
  });
  await new Promise((listening, failed) => {
    server.once('error', failed);
    server.listen(port, '127.0.0.1', listening);
  });
  servers.push(server);
  const kill = () => {
    try { server.close(); server.closeAllConnections(); } catch { /* closed */ }
  };
  return { base: `http://127.0.0.1:${server.address().port}`, port: server.address().port, misses, kill };
}

// ---- the browser ----------------------------------------------------------------------------------------------------
const MAC_CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const PATH_NAMES = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser', 'chrome'];
const runs = (file) => {
  try {
    accessSync(file, constants.X_OK);
    return statSync(file).isFile();
  } catch {
    return false;
  }
};
// The browser's executable, or '' when there is none: `path` when given (or else $CHROME) and nothing else then; with
// neither, the macOS application, then the usual names on PATH.
export function findChrome(path = '', env = process.env, platform = process.platform) {
  const named = path || env.CHROME || '';
  if (named !== '') return runs(named) ? named : '';
  if (platform === 'darwin' && runs(MAC_CHROME)) return MAC_CHROME;
  for (const dir of String(env.PATH || '').split(delimiter)) {
    for (const name of PATH_NAMES) if (dir !== '' && runs(join(dir, name))) return join(dir, name);
  }
  return '';
}
// Where findChrome looks, for the message of a runner that found none.
export const chromePlaces = (path = '', env = process.env, platform = process.platform) =>
  (path ? `--chrome ${path}` : env.CHROME ? `$CHROME (${env.CHROME})`
        : `${platform === 'darwin' ? `${MAC_CHROME}, then ` : ''}${PATH_NAMES.join(', ')} on PATH`);

// Chrome's own null sink, as a flag for `extra`: every AudioContext renders into nothing, at the pace of a device.
// For a machine with no audio device (a CI runner), where a context may say it runs and never render.
export const NULL_SINK = '--disable-audio-output';

// The GPU path: 'metal' (ANGLE on Metal), 'swiftshader' (the software renderer, as on a CI runner with no GPU), or
// the flags themselves. Not given: by platform, Metal on macOS and SwiftShader elsewhere.
export function gpuFlags(gpu = undefined, platform = process.platform) {
  if (Array.isArray(gpu)) return gpu;
  const which = gpu || (platform === 'darwin' ? 'metal' : 'swiftshader');
  if (which === 'metal') return ['--use-angle=metal'];
  if (which === 'swiftshader') return ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
  throw new Error(`cdp.mjs: gpu is 'metal', 'swiftshader' or a list of flags, not '${gpu}'`);
}

// One headless Chrome. Options:
//   width, height   the window (1280 x 800)
//   chrome          the executable (findChrome's choice otherwise: $CHROME, the macOS application, PATH)
//   profile         a --user-data-dir of the caller's, kept; '' makes a throwaway one under the system's temporary
//                   directory, removed at the end
//   gpu             'metal', 'swiftshader' or flags (gpuFlags); not given: $SCOUT_GPU (the scouts' variable, kept for
//                   the scenario), else by platform
//   autoplay        --autoplay-policy=no-user-gesture-required, so an AudioContext runs with no gesture (true)
//   sandbox         false adds --no-sandbox, for a container that gives Chrome no user namespace (true, unless the
//                   environment has FCMP_WEB_LIVE_NO_SANDBOX=1: the gate's switch reaches every Chrome started here)
//   flags, extra    more flags before and after the fixed ones (the last of two switches wins); flags not given:
//                   $SCOUT_FLAGS
//   png             where page.shot writes a relative name (PNG)
//   answerMs        how long one DevTools command may take (60 s)
// Answers { send, page, kill, close, version, renderer, audioRuns, profile, pid, path, flags, gone, said }.
export async function chrome({ width = 1280, height = 800, extra = [], profile = '', chrome: path = '',
                               gpu = undefined, flags = undefined, autoplay = true,
                               sandbox = process.env.FCMP_WEB_LIVE_NO_SANDBOX !== '1', png = PNG,
                               answerMs = 60000 } = {}) {
  const exe = findChrome(path);
  if (exe === '') throw Object.assign(new Error(`no Chrome (${chromePlaces(path)})`), { code: 'NO_CHROME' });
  const throwaway = profile === '';
  if (throwaway) {
    profile = mkdtempSync(join(tmpdir(), 'fcmp-chrome-'));
    profiles.push(profile);
  }
  const split = (text) => text.split(' ').filter((f) => f !== '');
  const all = ['--headless=new', '--remote-debugging-pipe', `--user-data-dir=${profile}`, '--no-first-run',
               '--no-default-browser-check', '--disable-extensions', '--mute-audio',
               // nothing of the browser's own goes to the network (updates, variations, reports)
               '--disable-background-networking', '--disable-component-update', '--disable-sync', '--no-pings',
               '--disable-default-apps', '--metrics-recording-only',
               ...(gpu === undefined && process.env.SCOUT_GPU ? split(process.env.SCOUT_GPU) : gpuFlags(gpu)),
               ...(flags === undefined ? split(process.env.SCOUT_FLAGS || '') : flags),
               ...(autoplay ? ['--autoplay-policy=no-user-gesture-required'] : []),
               ...(sandbox ? [] : ['--no-sandbox']),
               `--window-size=${width},${height}`, ...extra, 'about:blank'];
  // DevTools over the pipe: Chrome reads commands on its descriptor 3 and writes on 4, each message ended by a NUL.
  const browser = spawn(exe, all, { stdio: ['ignore', 'pipe', 'pipe', 'pipe', 'pipe'] });
  children.push(browser);
  let said = '';                                        // the end of what Chrome printed: why it did not start
  const hearSaid = (chunk) => { said = (said + chunk).slice(-4000); };
  browser.stdout.on('data', hearSaid);
  browser.stderr.on('data', hearSaid);
  const [toChrome, fromChrome] = [browser.stdio[3], browser.stdio[4]];
  toChrome.on('error', () => {});                       // a Chrome that went away: said by the commands that wait
  fromChrome.on('error', () => {});

  let nextId = 0;
  let gone = '';
  const waiting = new Map();
  const listeners = [];
  const lost = (why) => {
    if (gone !== '') return;
    gone = why;
    for (const { reject } of waiting.values()) reject(new Error(why));
    waiting.clear();
  };
  browser.on('error', (e) => lost(`Chrome did not start (${e.code || e.message}): ${exe}`));
  browser.on('exit', (code, signal) => lost(`Chrome went away (${signal || `exit ${code}`})`));
  const hear = (text) => {
    let msg = null;
    try {
      msg = JSON.parse(text);
    } catch {
      return;
    }
    if (msg.id !== undefined && waiting.has(msg.id)) {
      const { answer, reject } = waiting.get(msg.id);
      waiting.delete(msg.id);
      if (msg.error) reject(new Error(msg.error.message)); else answer(msg.result);
    } else if (msg.method) {
      for (const l of listeners) l(msg);
    }
  };
  let parts = [];
  fromChrome.on('data', (chunk) => {
    let from = 0;
    for (;;) {
      const end = chunk.indexOf(0, from);
      if (end < 0) {
        if (from < chunk.length) parts.push(chunk.subarray(from));
        return;
      }
      parts.push(chunk.subarray(from, end));
      const text = Buffer.concat(parts).toString('utf8');
      parts = [];
      from = end + 1;
      hear(text);
    }
  });
  const send = (method, params = {}, sessionId = undefined) => new Promise((answer, reject) => {
    if (gone !== '') { reject(new Error(gone)); return; }
    const id = ++nextId;
    const timer = setTimeout(() => {
      if (waiting.delete(id)) reject(new Error(`${method} unanswered`));
    }, answerMs);
    waiting.set(id, { answer: (v) => { clearTimeout(timer); answer(v); },
                      reject: (e) => { clearTimeout(timer); reject(e); } });
    toChrome.write(JSON.stringify({ id, method, params, sessionId }) + '\0');
  });

  const kill = () => {
    try { browser.kill('SIGKILL'); } catch { /* gone */ }
  };
  // The orderly end: Chrome is asked to close, killed if it has not within 2 s, and its throwaway profile removed.
  const close = async () => {
    const exited = new Promise((r) => {
      if (browser.exitCode !== null || browser.signalCode !== null) r(); else browser.once('exit', r);
    });
    send('Browser.close').catch(() => {});
    await within(2000, exited);
    kill();
    await within(2000, exited);
    if (children.includes(browser)) children.splice(children.indexOf(browser), 1);
    if (throwaway) {
      try { rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 }); } catch { /* left */ }
      if (profiles.includes(profile)) profiles.splice(profiles.indexOf(profile), 1);
    }
  };
  let product = null;
  try {
    product = await within(30000, send('Browser.getVersion'));
  } catch (e) {
    await close();
    throw new Error(`${e.message}${said.trim() ? `; it said: ${said.trim().split('\n').slice(-6).join(' | ')}` : ''}`);
  }
  if (product === LATE) {
    await close();
    throw new Error(`Chrome did not answer on its DevTools pipe in 30 s: ${exe}`);
  }

  async function page(url, { newWindow = false, w = width, h = height, png: pngDir = png } = {}) {
    const { targetId } = await send('Target.createTarget', newWindow
      ? { url: 'about:blank', width: w, height: h, newWindow } : { url: 'about:blank' });
    const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
    const consoleLines = [];                            // every console line, log entry and uncaught error
    const exceptions = [];                              // the uncaught errors alone
    const listener = (msg) => {
      if (msg.sessionId !== sessionId) return;
      if (msg.method === 'Runtime.consoleAPICalled') {
        consoleLines.push(`console.${msg.params.type}: `
                          + msg.params.args.map((a) => a.value ?? a.description).join(' '));
      } else if (msg.method === 'Runtime.exceptionThrown') {
        const d = msg.params.exceptionDetails;
        const line = `EXCEPTION: ${d.text} ${(d.exception || {}).description || ''}`;
        consoleLines.push(line);
        exceptions.push(line);
      } else if (msg.method === 'Log.entryAdded') {
        consoleLines.push(`log.${msg.params.entry.level}: ${msg.params.entry.text} ${msg.params.entry.url || ''}`);
      }
    };
    listeners.push(listener);
    const s = (method, params = {}) => send(method, params, sessionId);
    await s('Runtime.enable');
    await s('Log.enable');
    await s('Page.enable');
    await s('DOM.enable');
    const ev = async (expression) => {
      let r;
      try {
        r = await s('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
      } catch (e) {
        throw new Error(`${e.message} [${String(expression).slice(0, 90)}] visibility unknown`);
      }
      if (r.exceptionDetails) {
        const d = r.exceptionDetails;
        throw new Error(`evaluate: ${d.text} ${(d.exception || {}).description || ''}`);
      }
      return r.result.value;
    };
    const P = { s, ev, consoleLines, exceptions, targetId, sessionId };

    // ---- the device ----
    P.metrics = (w2, h2, dsf) =>
      s('Emulation.setDeviceMetricsOverride', { width: w2, height: h2, deviceScaleFactor: dsf, mobile: false });
    // Answers '' or why the navigation failed (the browser's own words), after `settle` ms either way.
    P.go = async (u, settle = 1200) => {
      const r = await s('Page.navigate', { url: u });
      await sleep(settle);
      return (r && r.errorText) || '';
    };
    P.reload = async (settle = 1500) => { await s('Page.reload', {}); await sleep(settle); };
    // The first truthy value of `expression`, asked every `every` ms for at most `ms`; null when there was none. An
    // expression that throws (the page is still loading) counts as not yet.
    P.until = async (expression, ms = 10000, every = 50) => {
      const deadline = Date.now() + ms;
      for (;;) {
        const value = await ev(expression).catch(() => null);
        if (value) return value;
        if (Date.now() >= deadline || gone !== '') return null;
        await sleep(every);
      }
    };
    P.close = async () => {
      if (listeners.includes(listener)) listeners.splice(listeners.indexOf(listener), 1);
      await send('Target.closeTarget', { targetId }).catch(() => {});
    };

    // ---- the page's own state ----
    P.status = async () => JSON.parse(await ev('Module.fcmpStatus()'));
    P.stats = async () => JSON.parse(await ev('fcmpPage.stats().then(JSON.stringify)'));
    P.a11y = async () => JSON.parse(await ev("typeof Module.fcmpA11y === 'function' ? Module.fcmpA11y() : 'null'"));
    P.canvas = async () => JSON.parse(await ev(`JSON.stringify((() => {
      const c = document.getElementById('fcmp-canvas');
      const r = c.getBoundingClientRect();
      return { x: r.left, y: r.top, w: r.width, h: r.height, bw: c.width, bh: c.height, dpr: devicePixelRatio,
               iw: innerWidth, ih: innerHeight, sx: scrollX, sy: scrollY };
    })())`));
    // Panel logical px -> client px, through the canvas box as it is NOW (the zoom may have changed)
    P.at = async (x, y) => { const c = await P.canvas(); return [c.x + x * c.w / 960, c.y + y * c.h / 640]; };

    // ---- real input ----
    let buttonsDown = 0;
    const mouse = (type, x, y, more = {}) =>
      s('Input.dispatchMouseEvent', { type, x, y, button: 'none', buttons: buttonsDown, pointerType: 'mouse',
                                      ...more });
    P.moveClient = (x, y) => mouse('mouseMoved', x, y);
    P.move = async (x, y) => { const [cx, cy] = await P.at(x, y); await mouse('mouseMoved', cx, cy); };
    P.pressClient = async (x, y, { button = 'left', clickCount = 1, modifiers = 0 } = {}) => {
      await mouse('mouseMoved', x, y, { modifiers });
      await sleep(30);
      buttonsDown = button === 'right' ? 2 : 1;
      await mouse('mousePressed', x, y, { button, buttons: buttonsDown, clickCount, modifiers });
    };
    P.releaseClient = async (x, y, { button = 'left', clickCount = 1, modifiers = 0 } = {}) => {
      buttonsDown = 0;
      await mouse('mouseReleased', x, y, { button, buttons: 0, clickCount, modifiers });
    };
    P.clickClient = async (x, y, o = {}) => {
      await P.pressClient(x, y, o);
      await sleep(40);
      await P.releaseClient(x, y, o);
    };
    P.click = async (x, y, o = {}) => {
      const [cx, cy] = await P.at(x, y);
      await P.clickClient(cx, cy, o);
      await sleep(o.settle ?? 250);
    };
    P.rightClick = async (x, y, o = {}) => P.click(x, y, { ...o, button: 'right' });
    P.doubleClick = async (x, y) => {
      const [cx, cy] = await P.at(x, y);
      await P.pressClient(cx, cy, { clickCount: 1 });
      await sleep(30);
      await P.releaseClient(cx, cy, { clickCount: 1 });
      await sleep(60);
      await P.pressClient(cx, cy, { clickCount: 2 });
      await sleep(30);
      await P.releaseClient(cx, cy, { clickCount: 2 });
      await sleep(300);
    };
    // a drag in Panel px from (x, y) by (dx, dy), in `steps` moves 16 ms apart; the button is released at the end
    P.drag = async (x, y, dx, dy, { steps = 20, hold = null, modifiers = 0 } = {}) => {
      const c = await P.canvas();
      const k = c.w / 960;
      const cx = c.x + x * k;
      const cy = c.y + y * k;
      await P.pressClient(cx, cy, { modifiers });
      for (let i = 1; i <= steps; i += 1) {
        await mouse('mouseMoved', cx + dx * k * i / steps, cy + dy * k * i / steps,
                    { button: 'left', buttons: 1, modifiers });
        await sleep(16);
      }
      if (hold) await hold();
      await P.releaseClient(cx + dx * k, cy + dy * k, { modifiers });
      await sleep(250);
    };
    P.wheel = async (x, y, deltaY, { deltaX = 0, modifiers = 0 } = {}) => {
      const [cx, cy] = await P.at(x, y);
      await mouse('mouseMoved', cx, cy);
      await s('Input.dispatchMouseEvent', { type: 'mouseWheel', x: cx, y: cy, deltaX, deltaY, modifiers,
                                            pointerType: 'mouse' });
      await sleep(120);
    };
    const VK = { Enter: 13, Escape: 27, Tab: 9, Backspace: 8, Delete: 46, ArrowUp: 38, ArrowDown: 40, ArrowLeft: 37,
                 ArrowRight: 39, Home: 36, End: 35, PageUp: 33, PageDown: 34, ' ': 32 };
    // modifiers: Alt 1, Ctrl 2, Meta 4, Shift 8. NEVER pass nativeVirtualKeyCode: on macOS headless Chrome the key
    // then repeats without end (8,500 keydowns in a second on a plain page) and starves the page's timers and
    // messages.
    P.key = async (key, { modifiers = 0, settle = 120 } = {}) => {
      const named = key.length > 1;
      const code = named ? key : (/[a-z]/i.test(key) ? `Key${key.toUpperCase()}` : /[0-9]/.test(key) ? `Digit${key}`
                                  : key === ' ' ? 'Space' : '');
      const vk = VK[key] ?? key.toUpperCase().charCodeAt(0);
      const text = named ? (key === 'Enter' ? '\r' : undefined) : ((modifiers & 6) ? undefined : key);
      await s('Input.dispatchKeyEvent', { type: text !== undefined ? 'keyDown' : 'rawKeyDown', key, code,
                                          windowsVirtualKeyCode: vk, modifiers, text, unmodifiedText: text });
      await s('Input.dispatchKeyEvent', { type: 'keyUp', key, code, windowsVirtualKeyCode: vk, modifiers });
      await sleep(settle);
    };
    P.type = async (textToType) => {
      for (const ch of textToType) await P.key(ch, { settle: 40 });
      await sleep(120);
    };

    // ---- a control by its accessibility title (Module.fcmpA11y) ----
    P.find = async (title, { parent = null, role = null, nth = 0 } = {}) => {
      const a = await P.a11y();
      if (!a) throw new Error('no Module.fcmpA11y');
      let par = null;
      if (parent !== null) {
        par = a.items.find((i) => i.title === parent);
        if (!par) throw new Error(`no a11y item "${parent}"`);
      }
      const hits = a.items.filter((i) => (title instanceof RegExp ? title.test(i.title) : i.title === title)
                                         && (par === null || i.parent === par.id)
                                         && (role === null || i.role === role));
      return hits[nth] || null;
    };
    P.need = async (title, o = {}) => {
      const it = await P.find(title, o);
      if (!it) throw new Error(`no a11y item "${title}"${o.parent ? ` in "${o.parent}"` : ''}`);
      return it;
    };
    P.clickItem = async (title, o = {}) => {
      const it = await P.need(title, o);
      await P.click(it.x + it.w / 2, it.y + it.h / 2, o);
      return it;
    };
    P.value = async (title, o = {}) => { const it = await P.find(title, o); return it ? it.value : null; };
    P.footer = () => P.value('Footer');

    // ---- FunkGui's DOM menu (WebServices): its rows, and a real click on one ----
    P.menu = async () => JSON.parse(await ev(`JSON.stringify((() => {
      const m = document.querySelector('[data-funkgui-menu]');
      if (!m) return null;
      const r = m.getBoundingClientRect();
      return { serial: m.getAttribute('data-funkgui-menu'), box: [r.left, r.top, r.width, r.height],
               focused: m.contains(document.activeElement),
               items: Array.from(m.children).map((el) => {
                 const b = el.getBoundingClientRect();
                 return { role: el.getAttribute('role'), text: el.textContent, id: el.getAttribute('data-id'),
                          disabled: el.getAttribute('aria-disabled') === 'true',
                          checked: el.getAttribute('aria-checked') === 'true',
                          x: b.left + b.width / 2, y: b.top + b.height / 2 };
               }) };
    })())`));
    P.menuChoose = async (text) => {
      const m = await P.menu();
      if (!m) throw new Error('no menu is open');
      const it = m.items.find((i) => i.text === text || (text instanceof RegExp && text.test(i.text)));
      if (!it) throw new Error(`no menu item ${text}: ${m.items.map((i) => i.text).join(' | ')}`);
      await sleep(300);
      await P.clickClient(it.x, it.y);
      await sleep(300);
      return it;
    };

    // ---- pictures ----
    // A PNG of the canvas's box (or of the viewport), written to `name`: as it is when absolute, else under the png
    // directory. `clip` (page px: x, y, width, height) replaces the canvas's box. Answers the file.
    P.shot = async (name, { canvasOnly = true, clip = null } = {}) => {
      if (clip === null && canvasOnly) {
        const c = await P.canvas();
        clip = { x: c.x + c.sx, y: c.y + c.sy, width: c.w, height: c.h };
      }
      const r = await s('Page.captureScreenshot', clip === null ? { format: 'png' }
        : { format: 'png', clip: { scale: 1, ...clip }, captureBeyondViewport: true });
      const file = isAbsolute(name) ? name : join(pngDir, name);
      mkdirSync(dirname(file), { recursive: true });
      writeFileSync(file, Buffer.from(r.data, 'base64'));
      return file;
    };

    // ---- START, and the taps on the port ----
    P.start = async () => {
      const at = JSON.parse(await ev(`JSON.stringify((() => {
        const r = document.getElementById('fcmp-start').getBoundingClientRect();
        return [r.left + r.width / 2, r.top + r.height / 2];
      })())`));
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
          if (kind === 1) {
            T.params.push({ t: performance.now(), v: Array.from(new Float32Array(m.slice(16, 136))),
                            snap: new Uint32Array(m.slice(136, 140))[0] });
          }
        }
        return orig(m, t);
      };
      port.addEventListener('message', (e) => {
        const b = e.data;
        if (!(b instanceof ArrayBuffer) || b.byteLength < 320) return;
        const u = new Uint32Array(b, 0, 80), f = new Float32Array(b, 0, 80), h = new Uint16Array(b, 0, 40);
        T.replies += 1;
        T.reply = { flags: u[4], latency: u[5], columns: u[6], publish: u[8], modeSlot: h[18], fadeFrom: h[19],
                    uiFlags: u[10], rate: f[11], frameLatency: u[12], fade: f[13], bypass: f[14],
                    inPeak: [f[16], f[17]], outPeak: [f[20], f[21]], gr: [f[32], f[33]],
                    blockMaxGr: [f[34], f[35]], thrDb: f[45], slope: f[46] };
      });
      return true;
    })()`);
    P.tapRead = async (clear = false) => JSON.parse(await ev(`JSON.stringify((() => {
      const T = globalThis.__tap;
      const r = { n: T.params.length, last: T.params[T.params.length - 1] || null, kinds: T.kinds, reply: T.reply,
                  replies: T.replies };
      ${clear ? 'T.params.length = 0;' : ''}
      return r;
    })())`));
    if (url) await P.go(url);
    return P;
  }

  // What draws a canvas here: the WebGL renderer's own name, asked of a throwaway canvas on a blank page ('' when
  // the browser gives no WebGL2).
  const renderer = async () => {
    const p = await page(null);
    try {
      return await p.ev(`(() => {
        const gl = document.createElement('canvas').getContext('webgl2');
        if (!gl) return '';
        const info = gl.getExtension('WEBGL_debug_renderer_info');
        const name = String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER));
        const lose = gl.getExtension('WEBGL_lose_context');
        if (lose) lose.loseContext();
        return name;
      })()`);
    } finally {
      await p.close();
    }
  };
  // Whether an AudioContext renders here: one made on a blank page runs, and its clock moves, within `ms`. Only a
  // Chrome started with `autoplay` can say yes. On a machine with no audio device it may say no: NULL_SINK as an
  // `extra` flag then gives a Chrome whose contexts render into nothing, at the same pace.
  const audioRuns = async (ms = 3000) => {
    const p = await page(null);
    try {
      return await p.ev(`(async () => {
        const context = new AudioContext();
        const t0 = performance.now();
        const runs = () => context.state === 'running' && context.currentTime > 0;
        while (!runs() && performance.now() - t0 < ${Number(ms)}) await new Promise((r) => setTimeout(r, 50));
        const ok = runs();
        await context.close().catch(() => {});
        return ok;
      })()`);
    } finally {
      await p.close();
    }
  };
  return { send, page, kill, close, renderer, audioRuns, version: product, profile, pid: browser.pid, path: exe,
           flags: all, gone: () => gone, said: () => said };
}

// The system pasteboard's change count (no content is read); -1 where it cannot be asked (not macOS).
export function pasteboardCount() {
  try {
    return Number(execFileSync('osascript', ['-l', 'JavaScript', '-e',
      'ObjC.import("AppKit"); $.NSPasteboard.generalPasteboard.changeCount'],
      { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }).trim());
  } catch {
    return -1;
  }
}

// fcdsp::Pid by name (Source/fcdsp/params/Pid.h): the index of a value in a Params record.
export const PID = { thr: 0, ratio: 1, knee: 2, range: 3, atk: 4, rel: 5, tmode: 6, hold: 7, look: 8, det: 9,
                     schpf: 10, sce: 11, link: 12, stmode: 13, voice: 14, drive: 15, makeup: 16, automu: 17, mix: 18,
                     s2thr: 19, s2atk: 20, s2rel: 21, mode: 22, extkey: 23, listen: 24, delta: 25, bypass: 26,
                     quality: 27, labudget: 28, output: 29 };

// Rows as they are printed and kept: row(true|false|null, id, detail), null being a NOTE. With a test name the line
// is the probes' own ("PASS     <test> <id>: <detail>"); without one it is the scouts' short form.
export class Report {
  constructor(test = '') {
    this.test = test;
    this.rows = [];
  }

  row(ok, id, detail) {
    this.rows.push({ ok, id, detail });
    const word = ok === null ? 'NOTE' : ok ? 'PASS' : 'FAIL';
    const text = typeof detail === 'string' ? detail : JSON.stringify(detail);
    console.log(this.test === '' ? `${word}  ${id}: ${text}` : `${word}     ${this.test} ${id}: ${text}`);
    return ok;
  }

  note(id, detail) {
    return this.row(null, id, detail);
  }
}
