#!/usr/bin/env node
// Scripts/web/page-check.mjs <site> --browser chrome|firefox|safari: the demo's self-test in a real browser, driven
// over WebDriver (ADR-93, the lead phase). The CI's browser legs run it on the downloaded site artifact.
//
// What it does: serves <site> from 127.0.0.1 (a server of its own: GET and HEAD, a Content-Type by extension, nothing
// else), starts the browser's driver (chromedriver, geckodriver, safaridriver), opens <page> (default fcmp-ui.html,
// which the page treats as ?selftest=1), reads document.title and #funkgui-log until the title is PASS or starts with
// FAIL, prints the log, saves a screenshot, ends the session and stops the driver and the server whatever happened.
// Exit: 0 the page said PASS; 1 it said FAIL; 2 the check could not run (no driver, no session, the browser or the
// driver went away, no verdict in time). The code is 2 until a PASS was read. No dependency: node >= 18 (fetch).
//
//   --browser <name>     chrome | firefox | safari (required)
//   --page <file>        the page, with an optional query (default fcmp-ui.html; index.html?selftest=1 is the same test)
//   --driver <path>      the driver (default: $CHROMEWEBDRIVER/chromedriver, $GECKOWEBDRIVER/geckodriver,
//                        /usr/bin/safaridriver; else the name on PATH). A .mjs/.js path runs under this node (tests).
//   --webdriver <url>    a driver that already runs (nothing is started or stopped)
//   --binary <path>      the browser itself (chrome, firefox)
//   --arg <flag>         one more browser argument (repeatable; chrome, firefox). The last of two switches wins.
//   --autoplay           let an AudioContext run with no gesture (chrome: the autoplay policy; firefox: its prefs), so
//                        the page judges the live audio too. Without it the context stays suspended, which the page
//                        expects and says. Safari has no such switch.
//   --headed             a window (for xvfb-run); chrome and firefox are headless otherwise. Safari is never headless.
//   --timeout <seconds>  the whole run's bound (default 120)
//   --screenshot <png>   where the picture goes (taken at the verdict and at a timeout)
//   --log <file>         the page's log and the verdict line, as printed
//   --safari-here        Safari outside GitHub Actions. It opens a window and plays sound: never on the lead's machine.
//   --serve [--port N]   no browser: serve <site> and print its address until interrupted (a hand test in any browser)
//
// Chrome is muted and Firefox's volume is zero. Chrome's GPU path is ANGLE on Metal on macOS and SwiftShader elsewhere,
// as FunkGui's tools/web/check-page.mjs chooses; Firefox is told to give WebGL on a software renderer.
import { spawn } from 'node:child_process';
import { createReadStream, existsSync, statSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { createServer as createSocket } from 'node:net';
import { extname, join, normalize, resolve, sep } from 'node:path';

const usage = 'usage: page-check.mjs <site> --browser chrome|firefox|safari [--page <file>] [--driver <path>] '
            + '[--webdriver <url>] [--binary <path>] [--arg <flag>]... [--autoplay] [--headed] [--timeout <s>] '
            + '[--screenshot <png>] [--log <file>] [--safari-here]';
const say = (text) => console.log(`page-check: ${text}`);

// ---- arguments ------------------------------------------------------------------------------------------------------
const opt = { dir: '', browser: '', page: 'fcmp-ui.html', driver: '', webdriver: '', binary: '', args: [],
              autoplay: false, headed: false, timeoutS: 120, screenshot: '', log: '', safariHere: false,
              serve: false, port: 0 };
{
  const takes = { '--browser': 'browser', '--page': 'page', '--driver': 'driver', '--webdriver': 'webdriver',
                  '--binary': 'binary', '--screenshot': 'screenshot', '--log': 'log' };
  const flags = { '--autoplay': 'autoplay', '--headed': 'headed', '--safari-here': 'safariHere', '--serve': 'serve' };
  const bad = () => { console.error(usage); process.exit(2); };
  for (let i = 2; i < process.argv.length; i += 1) {
    const a = process.argv[i];
    if (a in takes) opt[takes[a]] = process.argv[++i] || bad();
    else if (a in flags) opt[flags[a]] = true;
    else if (a === '--arg') opt.args.push(process.argv[++i] || bad());
    else if (a === '--timeout') opt.timeoutS = Number(process.argv[++i]);
    else if (a === '--port') opt.port = Number(process.argv[++i]);
    else if (a.startsWith('-') || opt.dir) bad();
    else opt.dir = a;
  }
  if (!opt.dir || !(opt.serve || ['chrome', 'firefox', 'safari'].includes(opt.browser)) || !(opt.timeoutS > 0)
      || !(opt.port >= 0 && opt.port < 65536)) bad();
  if (!/^[A-Za-z0-9][A-Za-z0-9._-]*\.html(\?[A-Za-z0-9=&_.-]*)?$/.test(opt.page)) bad();
}
process.exitCode = 2;                                   // whatever ends this process: never 0 without a PASS
const refuse = (why) => { console.error(`page-check: ${why}`); process.exit(2); };
opt.dir = resolve(opt.dir);
if (!existsSync(join(opt.dir, opt.page.split('?')[0]))) refuse(`${opt.dir} has no ${opt.page.split('?')[0]}`);
if (opt.browser === 'safari' && !opt.safariHere && process.env.GITHUB_ACTIONS !== 'true')
  refuse('Safari is driven only on a CI runner (it opens a window and plays sound); --safari-here overrides');

// ---- what a browser is asked for ------------------------------------------------------------------------------------
function capabilities(o, platform = process.platform) {
  const timeouts = { script: 30000, pageLoad: 60000 };
  if (o.browser === 'chrome') {
    const gl = platform === 'darwin' ? ['--use-angle=metal'] : ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
    const args = [...(o.headed ? [] : ['--headless=new']), '--mute-audio', '--no-first-run',
                  '--no-default-browser-check', '--disable-extensions', '--window-size=1200,900', ...gl,
                  ...(o.autoplay ? ['--autoplay-policy=no-user-gesture-required'] : []), ...o.args];
    return { browserName: 'chrome', timeouts, 'goog:chromeOptions': { args, ...(o.binary ? { binary: o.binary } : {}) } };
  }
  if (o.browser === 'firefox') {
    const prefs = { 'media.volume_scale': '0.0',        // a string pref
                    'webgl.force-enabled': true, 'webgl.disabled': false,
                    ...(o.autoplay ? { 'media.autoplay.default': 0, 'media.autoplay.blocking_policy': 0 } : {}) };
    const args = [...(o.headed ? [] : ['-headless']), '-width', '1200', '-height', '900', ...o.args];
    return { browserName: 'firefox', timeouts,
             'moz:firefoxOptions': { args, prefs, ...(o.binary ? { binary: o.binary } : {}) } };
  }
  return { browserName: 'safari', timeouts };           // no headless, no arguments, no preferences
}
function driverCommand(o, port) {
  const env = process.env;
  const found = (dir, name) => (dir && existsSync(join(dir, name)) ? join(dir, name) : name);
  const path = o.driver || { chrome: found(env.CHROMEWEBDRIVER, 'chromedriver'),
                             firefox: found(env.GECKOWEBDRIVER, 'geckodriver'),
                             safari: '/usr/bin/safaridriver' }[o.browser];
  const args = o.browser === 'chrome' ? [`--port=${port}`] : ['--port', String(port)];
  return /\.m?js$/.test(path) ? [process.execPath, [path, ...args]] : [path, args];
}

// ---- the server -----------------------------------------------------------------------------------------------------
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
                '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
                '.wasm': 'application/wasm', '.txt': 'text/plain; charset=utf-8', '.svg': 'image/svg+xml',
                '.json': 'application/json', '.png': 'image/png' };
const notServed = [];                                   // every request that was not a 200: said at the end
function serve(dir) {
  const server = createServer((request, response) => {
    const end = (status) => {
      notServed.push(`${status} ${request.method} ${request.url}`);
      response.writeHead(status, { 'Content-Type': 'text/plain' }).end(String(status));
    };
    if (request.method !== 'GET' && request.method !== 'HEAD') return end(405);
    let path = '';
    try {
      path = normalize(join(dir, decodeURIComponent(new URL(request.url, 'http://x').pathname)));
    } catch { return end(400); }
    if (path !== dir && !path.startsWith(dir + sep)) return end(403);
    if (existsSync(path) && statSync(path).isDirectory()) path = join(path, 'index.html');
    if (!existsSync(path) || !statSync(path).isFile()) return end(404);
    response.writeHead(200, { 'Content-Type': TYPES[extname(path)] || 'application/octet-stream',
                              'Content-Length': statSync(path).size, 'Cache-Control': 'no-store' });
    if (request.method === 'HEAD') return response.end();
    return createReadStream(path).pipe(response);
  });
  return new Promise((listening, failed) => {
    server.on('error', failed);
    server.listen(opt.port, '127.0.0.1', () => listening(server));
  });
}
const freePort = () => new Promise((got, failed) => {
  const s = createSocket();
  s.on('error', failed);
  s.listen(0, '127.0.0.1', () => { const { port } = s.address(); s.close(() => got(port)); });
});

// ---- the run and its end --------------------------------------------------------------------------------------------
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const deadline = Date.now() + opt.timeoutS * 1000;
const left = () => Math.max(1, deadline - Date.now());
let server = null, driver = null, driverSaid = '', base = '', session = '', log = '', ending = false;
const lines = [];                                       // what --log gets
function printLog() {
  if (log) process.stdout.write(log.endsWith('\n') ? log : `${log}\n`);
  lines.push(log);
  log = '';
}
class WebDriverError extends Error {
  constructor(error, message) { super(`${error}: ${message}`.split('\n')[0]); this.code = error; }
}
// One WebDriver command, bounded. `value` of the reply, or a WebDriverError, or a plain Error (no reply, not JSON).
async function command(method, path, body, boundMs = left()) {
  let response = null;
  try {
    response = await fetch(`${base}${path}`, { method, signal: AbortSignal.timeout(boundMs),
                                               headers: { 'Content-Type': 'application/json; charset=utf-8' },
                                               body: body === undefined ? undefined : JSON.stringify(body) });
  } catch (e) {
    throw new Error(e.name === 'TimeoutError' ? `${method} ${path.replace(session, '<id>')} was not answered`
                                              : `the driver does not answer (${(e.cause && e.cause.code) || e.message})`);
  }
  let reply = null;
  try { reply = await response.json(); } catch { throw new Error(`the driver's reply to ${method} ${path} is not JSON`); }
  const value = reply && reply.value;
  if (!response.ok || (value && typeof value === 'object' && typeof value.error === 'string'))
    throw new WebDriverError((value && value.error) || `HTTP ${response.status}`, (value && value.message) || '');
  return value;
}
async function finish(code, why = '') {
  if (ending) return;
  ending = true;
  printLog();                                           // the page's log as last read, on whichever path ends the run
  if (why) console.error(`page-check: ${why}`);
  if (session) {
    if (opt.screenshot) {
      try {
        const png = await command('GET', `/session/${session}/screenshot`, undefined, 10000);
        writeFileSync(opt.screenshot, Buffer.from(String(png), 'base64'));
        say(`screenshot: ${opt.screenshot}`);
      } catch (e) { say(`no screenshot (${e.message})`); }
    }
    try { await command('DELETE', `/session/${session}`, undefined, 5000); } catch { /* the driver is stopped below */ }
  }
  if (driver && driver.exitCode === null && driver.signalCode === null) {
    const gone = new Promise((r) => driver.once('exit', r));
    const signal = (s) => { try { process.kill(-driver.pid, s); } catch { try { driver.kill(s); } catch { /* gone */ } } };
    signal('SIGTERM');                                  // its group: the driver and the browser it started
    await Promise.race([gone, sleep(2000)]);
    signal('SIGKILL');
  }
  if (server) server.close();
  for (const line of notServed) say(`server: ${line}`);
  if (code === 2 && driverSaid.trim()) console.error(`page-check: the driver said:\n${driverSaid.trim().split('\n').slice(-15).join('\n')}`);
  if (opt.log) writeFileSync(opt.log, `${lines.join('')}${why ? `page-check: ${why}\n` : ''}`);
  process.exit(code);
}
process.on('SIGINT', () => finish(2, 'interrupted'));
process.on('SIGTERM', () => finish(2, 'terminated'));
process.on('uncaughtException', (e) => finish(2, `${(e && e.message) || e}`));

async function main() {
  server = await serve(opt.dir);
  const url = `http://127.0.0.1:${server.address().port}/${opt.page}`;
  if (opt.serve) {
    say(`serving ${opt.dir}: ${url} (interrupt to end)`);
    return new Promise(() => {});
  }

  if (opt.webdriver) {
    base = opt.webdriver.replace(/\/$/, '');
  } else {
    const port = await freePort();
    const [exe, args] = driverCommand(opt, port);
    driver = spawn(exe, args, { stdio: ['ignore', 'pipe', 'pipe'], detached: process.platform !== 'win32' });
    driver.stdout.on('data', (d) => { driverSaid += d; });
    driver.stderr.on('data', (d) => { driverSaid += d; });
    driver.on('error', (e) => finish(2, `${exe} did not start (${e.code || e.message})`));
    driver.on('exit', (code, signal) => finish(2, `the driver went away (${signal || `code ${code}`})`));
    base = `http://127.0.0.1:${port}`;
    let ready = false;
    const until = Math.min(deadline, Date.now() + 20000);
    while (!ready && Date.now() < until && !ending) {
      try { ready = (await command('GET', '/status', undefined, 1000)).ready === true; } catch { /* not yet */ }
      if (!ready) await sleep(100);
    }
    if (!ready) return finish(2, `${exe} was not ready in time (its /status never said ready)`);
  }

  const wanted = capabilities(opt);
  let made = null;
  try {
    made = await command('POST', '/session', { capabilities: { alwaysMatch: wanted } });
  } catch (e) { return finish(2, `no session (${e.message})`); }
  session = made.sessionId;
  const got = made.capabilities || {};
  say(`${url} in ${got.browserName || opt.browser} ${got.browserVersion || '?'} on ${got.platformName || process.platform}`
      + ` (${JSON.stringify(wanted[opt.browser === 'chrome' ? 'goog:chromeOptions' : 'moz:firefoxOptions'] || {})})`);

  try {
    await command('POST', `/session/${session}/url`, { url });
  } catch (e) { return finish(2, `the page did not load (${e.message})`); }

  // The title and the log in one read: the page writes a row's line before the title, so at the verdict the log is whole.
  const state = "return [document.title, (document.getElementById('funkgui-log') || {}).textContent || ''];";
  const isVerdict = (t) => t === 'PASS' || t.startsWith('FAIL');
  let title = '';
  let stopped = '';
  while (Date.now() < deadline && !ending) {
    try {
      const read = await command('POST', `/session/${session}/execute/sync`, { script: state, args: [] });
      if (Array.isArray(read)) [title, log] = read.map(String);
    } catch (e) {
      stopped = e.message;
      await sleep(300);                                 // a driver that died says so itself (its exit ends the run)
      if (ending) return undefined;
      if (e instanceof WebDriverError && /invalid session id|no such window|session deleted/.test(e.code + e.message))
        return finish(2, `the browser went away before a verdict (${e.message})`);
      break;
    }
    if (isVerdict(title)) break;
    await sleep(Math.min(250, left()));
  }
  if (ending) return undefined;
  printLog();
  if (!isVerdict(title))
    return finish(2, `no verdict within ${opt.timeoutS} s (${stopped ? `the page stopped answering: ${stopped}; ` : ''}`
                     + `the title was '${title}')`);
  // What drew the canvas, for the record: asked of a throwaway canvas, after the verdict.
  try {
    const renderer = await command('POST', `/session/${session}/execute/sync`, { args: [], script:
      "const gl = document.createElement('canvas').getContext('webgl2'); if (!gl) return 'no WebGL2';"
      + "const i = gl.getExtension('WEBGL_debug_renderer_info');"
      + 'return String(gl.getParameter(i ? i.UNMASKED_RENDERER_WEBGL : gl.RENDERER));' }, 5000);
    say(`renderer: ${renderer}`);
    lines.push(`page-check: renderer: ${renderer}\n`);
  } catch { /* a note only */ }
  say(title);
  lines.push(`page-check: ${title}\n`);
  return finish(title === 'PASS' ? 0 : 1);
}

main().catch((e) => finish(2, e.message));
