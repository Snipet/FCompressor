// Scripts/web/cdp.mjs: the one library for driving headless Chrome (ADR-93, the web lead phase). Scripts/web/live.mjs,
// the runner of the browser gate, and Scripts/web/scenario.mjs, the scripted user, are written on it. Node only, no
// dependency; node 22 or later (checked below).
//
//   serve(site, { live, expect })   the gate's static server on 127.0.0.1, a free port: / is the site (exactly the
//                                   shipped files), /live/ the live directory, /expect/ the expectation directory, so
//                                   a live page reaches the shipped files as ../<name>. GET and HEAD, application/wasm,
//                                   no caching, nothing above a root in any spelling. It keeps what it was asked.
//   findChrome(path)                the browser: the path given, else $CHROME, the macOS application, a name on PATH.
//   chrome({ ... })                 one headless Chrome: a throwaway profile, ALWAYS --mute-audio (it plays through
//                                   the machine's speakers otherwise), the GPU flag by platform (ANGLE on Metal on
//                                   macOS, SwiftShader elsewhere), nothing fetched in the background. DevTools goes
//                                   over a pipe, not a port: a Chrome whose driver is gone ends by itself.
//   browser.page(url)               a tab: evaluate, real input events (mouse, keys, wheel), a screenshot, the demo
//                                   page's own state, the taps on the worklet's port, and a gate that answers one
//                                   address in the server's place (a file that is missing, or never comes).
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
                '.fp': 'text/plain; charset=utf-8', '.svg': 'image/svg+xml', '.png': 'image/png',
                '.wav': 'audio/wav' };

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
// at its end), port, misses (every request not answered 200, as "<status> <method> <url>"), asked (every request, in
// order: { method, url, cache: its Cache-Control header, '' when it has none }: a fetch that goes round the browser's
// cache says no-cache there), kill() }.
export async function serve(dir, { live = '', expect = '', port = 0 } = {}) {
  const site = resolve(dir);
  const roots = [['/live/', live], ['/expect/', expect]].filter(([, d]) => d !== '').map(([p, d]) => [p, resolve(d)]);
  const misses = [];
  const asked = [];
  const server = createServer((request, response) => {
    asked.push({ method: request.method, url: request.url, cache: String(request.headers['cache-control'] || '') });
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
  return { base: `http://127.0.0.1:${server.address().port}`, port: server.address().port, misses, asked, kill };
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
//   gpu             'metal', 'swiftshader' or flags (gpuFlags); not given: by platform
//   autoplay        --autoplay-policy=no-user-gesture-required, so an AudioContext runs with no gesture (true)
//   sandbox         false adds --no-sandbox, for a container that gives Chrome no user namespace (true, unless the
//                   environment has FCMP_WEB_LIVE_NO_SANDBOX=1: the gate's switch reaches every Chrome started here)
//   flags, extra    more flags before and after the fixed ones (the last of two switches wins; none by default)
//   png             where page.shot writes a relative name (PNG)
//   answerMs        how long one DevTools command may take (60 s)
// No other variable of the environment changes a flag: a Chrome is started as its caller says.
// Answers { send, page, kill, close, version, renderer, audioRuns, profile, pid, path, flags, gone, said }.
export async function chrome({ width = 1280, height = 800, extra = [], profile = '', chrome: path = '',
                               gpu = undefined, flags = [], autoplay = true,
                               sandbox = process.env.FCMP_WEB_LIVE_NO_SANDBOX !== '1', png = PNG,
                               answerMs = 60000 } = {}) {
  const exe = findChrome(path);
  if (exe === '') throw Object.assign(new Error(`no Chrome (${chromePlaces(path)})`), { code: 'NO_CHROME' });
  const throwaway = profile === '';
  if (throwaway) {
    profile = mkdtempSync(join(tmpdir(), 'fcmp-chrome-'));
    profiles.push(profile);
  }
  const all = ['--headless=new', '--remote-debugging-pipe', `--user-data-dir=${profile}`, '--no-first-run',
               '--no-default-browser-check', '--disable-extensions', '--mute-audio',
               // nothing of the browser's own goes to the network (updates, variations, reports)
               '--disable-background-networking', '--disable-component-update', '--disable-sync', '--no-pings',
               '--disable-default-apps', '--metrics-recording-only',
               ...gpuFlags(gpu), ...flags,
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
    let stopped = null;                                 // P.gate()'s: told of every request the tab's gate stops
    const listener = (msg) => {
      if (msg.sessionId !== sessionId) return;
      if (msg.method === 'Fetch.requestPaused') {
        if (stopped !== null) stopped(msg.params);
      } else if (msg.method === 'Runtime.consoleAPICalled') {
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
    // expression that throws (the page is still loading) counts as not yet, and so does a question the page does not
    // answer (a main thread that never rests): each question waits for the time that is left at most, never for
    // `answerMs`, so the bound holds either way.
    P.until = async (expression, ms = 10000, every = 50) => {
      const deadline = Date.now() + ms;
      for (;;) {
        const value = await within(Math.max(deadline - Date.now(), 1), ev(expression).catch(() => null));
        if (value && value !== LATE) return value;
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
    // P.key(key) sends one key, down and up. `key` is a named key of NAMED, or one character. A character is sent as
    // its text, with the code and the Windows virtual key code of the key that types it on a US keyboard: A-Z (either
    // case), 0-9, space, and the punctuation of the main block, shifted or not (`-` is Minus 189, `.` Period 190, `(`
    // Digit9 57, `"` Quote 222). Any other character (é, €) has no key: code '' and virtual key code 0, and arrives as
    // its text alone. Never the character's own code as the key's: 46 ('.') is Delete, 39 ("'") ArrowRight, 45 ('-')
    // Insert, and Chrome would run that key's editing command and drop the character. Shift is not added for a shifted
    // character or a capital: a listener that needs it is given it in `modifiers` (Alt 1, Ctrl 2, Meta 4, Shift 8).
    // NEVER pass nativeVirtualKeyCode: on macOS headless Chrome the key then repeats without end (8,500 keydowns in a
    // second on a plain page) and starves the page's timers and messages.
    const NAMED = { Enter: 13, Escape: 27, Tab: 9, Backspace: 8, Delete: 46, ArrowUp: 38, ArrowDown: 40,
                    ArrowLeft: 37, ArrowRight: 39, Home: 36, End: 35, PageUp: 33, PageDown: 34 };
    const KEYS = { ' ': ['Space', 32] };                // a character -> [code, Windows virtual key code]
    for (let i = 0; i < 26; i += 1) {
      const c = String.fromCharCode(65 + i);
      KEYS[c] = KEYS[c.toLowerCase()] = [`Key${c}`, 65 + i];
    }
    for (let i = 0; i < 10; i += 1) KEYS[String(i)] = KEYS[')!@#$%^&*('[i]] = [`Digit${i}`, 48 + i];
    for (const [code, vk, chars] of [['Backquote', 192, '`~'], ['Minus', 189, '-_'], ['Equal', 187, '=+'],
                                     ['BracketLeft', 219, '[{'], ['BracketRight', 221, ']}'], ['Backslash', 220, '\\|'],
                                     ['Semicolon', 186, ';:'], ['Quote', 222, '\'"'], ['Comma', 188, ',<'],
                                     ['Period', 190, '.>'], ['Slash', 191, '/?']]) {
      for (const c of chars) KEYS[c] = [code, vk];
    }
    P.key = async (key, { modifiers = 0, settle = 120 } = {}) => {
      const named = [...key].length !== 1;
      if (named && !(key in NAMED)) {
        throw new Error(`P.key: '${key}' is not a key: one character, or one of ${Object.keys(NAMED).join(', ')}`);
      }
      const [code, vk] = named ? [key, NAMED[key]] : KEYS[key] || ['', 0];
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

    // ---- one address of the tab, answered here in place of the server ----
    // P.gate(pattern) stops every request of this tab whose address matches `pattern` (the Fetch domain's own
    // patterns: * is any text) before it reaches the network, and answers it as gate.answer says at that moment:
    //   'pass'      it goes on to the server, unchanged (the answer a gate begins with)
    //   'missing'   404, as a server answers that does not have the file
    //   'fail'      no answer: the connection is refused
    //   'hold'      it waits, as with a server that never answers, until gate.release(how) answers every request
    //               that waits as `how` says: 'pass', 'missing' or 'fail'
    // gate.requests has every request stopped, in order: { url, headers (the request's own), how (how it was
    // answered; 'hold' while it waits), gone (the page had given the request up before it was answered: the browser
    // then refuses the answer), done (a promise: the browser has taken the answer, or refused it) }. gate.end() lets
    // what still waits pass and stops no more. A tab has one gate at a time. Nothing of the tab's other requests is
    // touched, and neither the server nor the site: for a row that needs one file out of reach.
    P.gate = async (pattern) => {
      const gate = { answer: 'pass', requests: [] };
      const answer = async (r, how) => {
        r.how = how;
        try {
          if (how === 'missing') {
            await s('Fetch.fulfillRequest', { requestId: r.id, responseCode: 404,
              responseHeaders: [{ name: 'Content-Type', value: 'text/plain; charset=utf-8' },
                                { name: 'Cache-Control', value: 'no-store' }],
              body: Buffer.from('404\n').toString('base64') });
          } else if (how === 'fail') {
            await s('Fetch.failRequest', { requestId: r.id, errorReason: 'ConnectionRefused' });
          } else {
            await s('Fetch.continueRequest', { requestId: r.id });
          }
        } catch {
          r.gone = true;
        }
      };
      stopped = (params) => {
        const r = { id: params.requestId, url: params.request.url, headers: params.request.headers, how: 'hold',
                    gone: false, done: null };
        gate.requests.push(r);
        if (!['pass', 'missing', 'fail', 'hold'].includes(gate.answer)) {
          throw new Error(`P.gate: the answer is 'pass', 'missing', 'fail' or 'hold', not '${gate.answer}'`);
        }
        if (gate.answer !== 'hold') r.done = answer(r, gate.answer);
      };
      gate.release = async (how = 'pass') => {
        const waiting = gate.requests.filter((r) => r.done === null);
        for (const r of waiting) r.done = answer(r, how);
        await Promise.all(waiting.map((r) => r.done));
        return waiting;
      };
      gate.end = async () => {
        await gate.release('pass');
        stopped = null;
        await s('Fetch.disable').catch(() => {});
      };
      await s('Fetch.enable', { patterns: [{ urlPattern: pattern, requestStage: 'Request' }] });
      return gate;
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
    // And the engine's input by the replies' columns (HistoryColumn, 32 bytes each after the head's 320: one a
    // millisecond of audio, its first float the largest input sample of that millisecond, both channels, in dBFS).
    // A reply's frame holds a meter that has fallen by the time it is read (40 ms release); a column holds its
    // millisecond exactly. T.heard is what the columns said since tapHear() was last called: `n` of them, the
    // quietest (`min`) and the loudest (`max`), and `lost`, how many the engine did not deliver in between (its ring
    // lapped the reader, or its count began again). The first `keep` of them are also kept one by one, in the order
    // they came (tapHear(skip, keep); tapLevels() reads them): for a row that needs how the input rose, not only how
    // far, or that compares them with a loop millisecond by millisecond.
    // A column is the larger of the two sides, so it does not tell one side from the other. The frame's two input
    // meters do: T.heard also counts the replies those columns came in (`replies`), and in how many of them the left
    // meter stood more than 1 dB over the right (`left`) and the right over the left (`right`).
    P.tap = () => ev(`(() => {
      const port = fcmpPage.node().port;
      const T = globalThis.__tap = { params: [], kinds: {}, reply: null, replies: 0,
                                     heard: { next: null, skip: 0, n: 0, min: null, max: null, lost: 0, keep: 0,
                                              levels: [], replies: 0, left: 0, right: 0 } };
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
        const H = T.heard;
        const count = Math.min(u[6], (b.byteLength - 320) >> 5);
        if (count === 0) return;
        const first = u[7];                             // the index of the first column delivered
        if (H.next !== null && first !== H.next) H.lost += first > H.next ? first - H.next : 1;
        H.next = (first + count) >>> 0;
        const columns = new Float32Array(b, 320, count * 8);
        const before = H.n;
        for (let k = 0; k < count; k += 1) {
          if (H.skip > 0) { H.skip -= 1; continue; }
          const db = columns[k * 8];
          H.n += 1;
          if (H.levels.length < H.keep) H.levels.push(db);
          if (H.min === null || db < H.min) H.min = db;
          if (H.max === null || db > H.max) H.max = db;
        }
        if (H.n === before) return;                     // every column of this reply was skipped
        H.replies += 1;
        if (f[16] > f[17] + 1) H.left += 1;
        else if (f[17] > f[16] + 1) H.right += 1;
      });
      return true;
    })()`);
    P.tapRead = async (clear = false) => JSON.parse(await ev(`JSON.stringify((() => {
      const T = globalThis.__tap;
      const r = { n: T.params.length, last: T.params[T.params.length - 1] || null, kinds: T.kinds, reply: T.reply,
                  replies: T.replies,
                  heard: { n: T.heard.n, min: T.heard.min, max: T.heard.max, lost: T.heard.lost,
                           replies: T.heard.replies, left: T.heard.left, right: T.heard.right } };
      ${clear ? 'T.params.length = 0;' : ''}
      return r;
    })())`));
    // T.heard begins again, with the columns after the next `skip` (the milliseconds a change of source takes); the
    // first `keep` of them are kept one by one.
    P.tapHear = (skip = 0, keep = 0) => ev(`(() => {
      Object.assign(globalThis.__tap.heard, { skip: ${Number(skip)}, n: 0, min: null, max: null, lost: 0,
                                              keep: ${Number(keep)}, levels: [], replies: 0, left: 0, right: 0 });
      return true;
    })()`);
    // The columns kept since tapHear(skip, keep), in dBFS, oldest first. A column of digital silence is -Infinity
    // (JSON has no such number: it travels as null).
    P.tapLevels = async () => JSON.parse(await ev('JSON.stringify(globalThis.__tap.heard.levels)'))
      .map((db) => (db === null ? -Infinity : db));
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
