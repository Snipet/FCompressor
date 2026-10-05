#!/usr/bin/env node
// Scripts/web/page-check.mjs <site> --browser chrome|firefox|safari: the demo's self-test, and with --frames and
// --pages the gate's other pages, in a real browser driven over WebDriver (ADR-93, the lead phase). CI's browser legs
// run it on the downloaded site: it is how Firefox and Safari are reached (Chrome's own gate is Scripts/web-live.sh,
// over DevTools). WebDriver classic over HTTP, no dependency: node 22 or later.
//
// What it does, in order:
//   the server     its own, on 127.0.0.1 and a free port, by the gate's map: / is <site>, /live/ is --live, /expect/
//                  is --expect. GET and HEAD, a Content-Type by extension, no caching, nothing above a root.
//   the driver     chromedriver, geckodriver or safaridriver on a free port. Its version is printed first; every
//                  command sent, every reply and everything the driver prints go to --driver-log.
//   the session    New Session with what the browser is asked for (capabilities() below); what it answers is printed.
//   the self-test  index.html?selftest=1: document.title and #funkgui-log are read until the title is PASS or starts
//                  with FAIL. The log is printed, the WebGL renderer named, a screenshot saved.
//   --frames       the gate's six capture pages (theme 0): Module.fcmpFrame() against <expect>/<view>.theme0.node.fp
//                  as the gate compares them: every line but `live` and `hooks`, and the hooks line must say dpi 2, a
//                  fixed clock, drawn and idle.
//   --pages        every *.html of the live directory, by the live-page protocol (the self-test's title and log).
//   the end        the session is deleted (while the driver is there to ask), and the driver's process group, the
//                  browser it started with it, is killed whatever happened: the driver's own exit too.
// A part that fails does not stop the run: the parts after it are still judged.
//
// Lines: `page-check: ...` for what the runner says; the pages' own PASS|FAIL|NOTE lines; one line a part, `PASS|FAIL
// selftest: ...`, `EQUAL|DIFFERS|FAIL <view>.theme0: ...` (a differing line under it: `<name>: browser <v>, node <v>`),
// `PASS|FAIL live/<page>: ...`; then `page-check: N/M passed` and last `page-check: PASS` or `page-check: FAIL: <the
// first part that failed>`.
// Exit: 0 every part passed; 1 a part failed; 2 the check could not run (usage, no driver, no session, the browser or
// the driver went away, no verdict in time). The code is 2 until the last part has been judged. When it could not run
// the message says what was sent, what came back and where the driver's log is.
//
//   --browser <name>     chrome | firefox | safari (required)
//   --live <dir>         the live directory, served at /live/ (the build's `live`)
//   --expect <dir>       the expectation directory, served at /expect/ and read by --frames
//   --frames             the six capture pages against the node values (needs --expect)
//   --pages              the live pages (needs --live)
//   --autoplay           let an AudioContext run with no gesture (chrome: the autoplay policy; firefox: its prefs), so
//                        the page judges the live audio too. Without it the context stays suspended, which the page
//                        expects and says. Safari has no such switch.
//   --timeout <seconds>  the whole run's bound (default 300)
//   --log <file>         what was printed, the failure included
//   --driver-log <file>  the exchange with the driver and the driver's own output; always written (default: under
//                        the system's temporary directory, and the path is printed)
//   --screenshot <png>   a picture at the self-test's verdict, and again of the page showing when the run cannot go on
//                        (the directories of these three files are made when they are missing)
//   --driver <path>      the driver (default: $CHROMEWEBDRIVER/chromedriver, $GECKOWEBDRIVER/geckodriver,
//                        /usr/bin/safaridriver; else the name on PATH). A .mjs/.js path runs under this node (tests).
//   --binary <path>      the browser itself (chrome, firefox)
//   --arg <flag>         one more browser argument (repeatable; chrome, firefox). The last of two switches wins.
//   --headed             a window (for xvfb-run); chrome and firefox are headless otherwise. Safari is never headless.
//   --safari-here        Safari outside GitHub Actions. It opens a window and plays sound: never on the lead's machine.
//
// Chrome is muted and Firefox's volume is zero. Chrome's GPU path is ANGLE on Metal on macOS and SwiftShader elsewhere,
// as the gate chooses; Firefox is told to give WebGL on a software renderer. Safari is taken as it is.
import { execFile, spawn } from 'node:child_process';
import { appendFileSync, createReadStream, existsSync, mkdirSync, readFileSync, readdirSync, statSync,
         writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { createServer as createSocket } from 'node:net';
import { homedir, tmpdir } from 'node:os';
import { dirname, extname, join, normalize, resolve, sep } from 'node:path';

const usage = 'usage: page-check.mjs <site> --browser chrome|firefox|safari [--live <dir>] [--expect <dir>] [--frames] '
            + '[--pages] [--autoplay] [--timeout <s>] [--log <file>] [--driver-log <file>] [--screenshot <png>] '
            + '[--driver <path>] [--binary <path>] [--arg <flag>]... [--headed] [--safari-here]';

// The gate's contract (docs/sprints/web-lead.md): the capture pages and their pins.
const VIEWS = ['panel', 'chars.sidechain', 'chars.colour', 'modebrowser', 'presetbrowser', 'settings'];
const PINS = 'theme=0&zoom=100&scale=2&dt=0.0166666675&nohint=1&nolive=1&host=WEB-LIVE';

// ---- arguments ------------------------------------------------------------------------------------------------------
const opt = { site: '', browser: '', live: '', expect: '', frames: false, pages: false, autoplay: false, headed: false,
              timeoutS: 300, log: '', driverLog: '', screenshot: '', driver: '', binary: '', args: [],
              safariHere: false };
{
  const takes = { '--browser': 'browser', '--live': 'live', '--expect': 'expect', '--log': 'log',
                  '--driver-log': 'driverLog', '--screenshot': 'screenshot', '--driver': 'driver',
                  '--binary': 'binary' };
  const flags = { '--frames': 'frames', '--pages': 'pages', '--autoplay': 'autoplay', '--headed': 'headed',
                  '--safari-here': 'safariHere' };
  const bad = (why) => { console.error(`page-check: ${why}\n${usage}`); process.exit(2); };
  const argv = process.argv.slice(2);
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    const value = () => (i + 1 < argv.length ? argv[++i] : bad(`${a} needs a value`));
    if (a in takes) opt[takes[a]] = value();
    else if (a in flags) opt[flags[a]] = true;
    else if (a === '--arg') opt.args.push(value());
    else if (a === '--timeout') opt.timeoutS = Number(value());
    else if (a.startsWith('-')) bad(`unknown option ${a}`);
    else if (opt.site) bad(`one site only (${opt.site}, ${a})`);
    else opt.site = a;
  }
  if (!opt.site) bad('no site');
  if (!['chrome', 'firefox', 'safari'].includes(opt.browser)) bad('--browser is chrome, firefox or safari');
  if (!(opt.timeoutS > 0)) bad('--timeout is a number of seconds');
  if (opt.frames && !opt.expect) bad('--frames needs --expect <dir>');
  if (opt.pages && !opt.live) bad('--pages needs --live <dir>');
}
process.exitCode = 2;                                   // whatever ends this process: never 0 without a PASS
const refuse = (why) => { console.error(`page-check: ${why}`); process.exit(2); };
if (Number(process.versions.node.split('.')[0]) < 22) refuse(`node 22 or later is needed (this is ${process.version})`);
opt.site = resolve(opt.site);
if (!existsSync(join(opt.site, 'index.html'))) refuse(`${opt.site} has no index.html`);
for (const key of ['live', 'expect']) {
  if (!opt[key]) continue;
  opt[key] = resolve(opt[key]);
  if (!existsSync(opt[key]) || !statSync(opt[key]).isDirectory()) refuse(`--${key} ${opt[key]} is not a directory`);
}
if (opt.browser === 'safari' && !opt.safariHere && process.env.GITHUB_ACTIONS !== 'true')
  refuse('Safari is driven only on a CI runner (it opens a window and plays sound); --safari-here overrides');
opt.driverLog = resolve(opt.driverLog || join(tmpdir(), `page-check-${opt.browser}-driver.log`));
for (const file of [opt.driverLog, opt.log, opt.screenshot]) {      // a CI step need not make their directories first
  if (!file) continue;
  try { mkdirSync(dirname(resolve(file)), { recursive: true }); } catch (e) { refuse(`${file}: ${e.message}`); }
}

// ---- what a browser is asked for ------------------------------------------------------------------------------------
function capabilities(o, platform = process.platform) {
  if (o.browser === 'chrome') {
    const gl = platform === 'darwin' ? ['--use-angle=metal']
                                     : ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
    const args = [...(o.headed ? [] : ['--headless=new']), '--mute-audio', '--no-first-run',
                  '--no-default-browser-check', '--disable-extensions', '--window-size=1200,900', ...gl,
                  ...(o.autoplay ? ['--autoplay-policy=no-user-gesture-required'] : []), ...o.args];
    return { browserName: 'chrome', 'goog:chromeOptions': { args, ...(o.binary ? { binary: o.binary } : {}) } };
  }
  if (o.browser === 'firefox') {
    const prefs = { 'media.volume_scale': '0.0',        // a string pref
                    'webgl.force-enabled': true, 'webgl.disabled': false,
                    ...(o.autoplay ? { 'media.autoplay.default': 0, 'media.autoplay.blocking_policy': 0 } : {}) };
    const args = [...(o.headed ? [] : ['-headless']), '-width', '1200', '-height', '900', ...o.args];
    return { browserName: 'firefox', 'moz:firefoxOptions': { args, prefs, ...(o.binary ? { binary: o.binary } : {}) } };
  }
  return { browserName: 'safari' };                     // as it is: no headless, no arguments, no preferences
}
// The driver's command line: each driver's own spelling of the port, and its most talkative log (to --driver-log).
function driverCommand(o, port) {
  const env = process.env;
  const found = (dir, name) => (dir && existsSync(join(dir, name)) ? join(dir, name) : name);
  const path = o.driver || { chrome: found(env.CHROMEWEBDRIVER, 'chromedriver'),
                             firefox: found(env.GECKOWEBDRIVER, 'geckodriver'),
                             safari: '/usr/bin/safaridriver' }[o.browser];
  const args = { chrome: [`--port=${port}`, '--verbose'], firefox: ['--port', String(port), '--log', 'debug'],
                 safari: ['--port', String(port), '--diagnose'] }[o.browser];
  // A snap Firefox cannot read a profile under /tmp, where geckodriver makes it: the profile goes where the snap can.
  if (o.browser === 'firefox' && process.platform === 'linux' && !o.binary && existsSync('/snap/firefox/current')) {
    const root = join(homedir(), 'snap', 'firefox', 'common', 'page-check');
    mkdirSync(root, { recursive: true });
    args.push('--profile-root', root);
  }
  const script = /\.m?js$/.test(path);
  return { path, exe: script ? process.execPath : path, args: script ? [path, ...args] : args,
           versionArgs: script ? [path, '--version'] : ['--version'] };
}

// ---- the server -----------------------------------------------------------------------------------------------------
const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
                '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
                '.wasm': 'application/wasm', '.txt': 'text/plain; charset=utf-8', '.fp': 'text/plain; charset=utf-8',
                '.svg': 'image/svg+xml', '.json': 'application/json', '.png': 'image/png', '.wav': 'audio/wav' };
const notServed = [];                                   // every request that was not a 200: said at the end
function serve(roots) {
  const server = createServer((request, response) => {
    const end = (status) => {
      notServed.push(`${status} ${request.method} ${request.url}`);
      response.writeHead(status, { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store' }).end(String(status));
    };
    if (request.method !== 'GET' && request.method !== 'HEAD') return end(405);
    let wanted = '';
    try { wanted = decodeURIComponent(new URL(request.url, 'http://x').pathname); } catch { return end(400); }
    // The longest prefix that has a root: /live/ and /expect/ before /.
    const [prefix, root] = roots.find(([p, dir]) => dir && wanted.startsWith(p));
    let path = normalize(join(root, wanted.slice(prefix.length)));
    if (path !== root && !path.startsWith(root + sep)) return end(403);
    if (existsSync(path) && statSync(path).isDirectory()) path = join(path, 'index.html');
    if (!existsSync(path) || !statSync(path).isFile()) return end(404);
    response.writeHead(200, { 'Content-Type': TYPES[extname(path)] || 'application/octet-stream',
                              'Content-Length': statSync(path).size, 'Cache-Control': 'no-store' });
    if (request.method === 'HEAD') return response.end();
    return createReadStream(path).pipe(response);
  });
  return new Promise((listening, failed) => {
    server.on('error', failed);
    server.listen(0, '127.0.0.1', () => listening(server));
  });
}
const freePort = () => new Promise((got, failed) => {
  const s = createSocket();
  s.on('error', failed);
  s.listen(0, '127.0.0.1', () => { const { port } = s.address(); s.close(() => got(port)); });
});

// ---- what is said, and the driver's log -----------------------------------------------------------------------------
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const started = Date.now();
const deadline = started + opt.timeoutS * 1000;
const left = () => Math.max(1, deadline - Date.now());
const lines = [];                                       // what --log gets
const out = (text) => { console.log(text); lines.push(text); };
const err = (text) => { console.error(text); lines.push(text); };
const short = (text, most = 2000) =>
  (text.length <= most ? text : `${text.slice(0, most)}... (${text.length} characters)`);

let logAtLineStart = true;
function toDriverLog(text, ownLine = false) {
  const whole = ownLine ? `${logAtLineStart ? '' : '\n'}${text}\n` : text;
  if (whole === '') return;
  logAtLineStart = whole.endsWith('\n');
  try { appendFileSync(opt.driverLog, whole); } catch { /* the run goes on without the file */ }
}
// One exchange into the driver's log; a poll that reads the same again is counted, not written again.
let lastExchange = '', repeats = 0;
function recordExchange(sent, got) {
  const both = `${sent}\n${got}`;
  if (both === lastExchange) { repeats += 1; return; }
  if (repeats > 0) toDriverLog(`page-check: (the same ${repeats} more time(s))`, true);
  lastExchange = both;
  repeats = 0;
  const at = ((Date.now() - started) / 1000).toFixed(3).padStart(8);
  toDriverLog(`page-check: [${at}] > ${short(sent)}\npage-check: [${at}] < ${short(got)}`, true);
}

// ---- WebDriver ------------------------------------------------------------------------------------------------------
let server = null, driver = null, driverSaid = '', driverGone = '', base = '', session = '', ending = false;
let pageLog = '';                                       // the page's log as last read, not yet printed
let asking = '';                                        // the command that waits for its reply
// A command that did not give its value: what was sent and what came back. status 0: no reply at all.
class Exchange extends Error {
  constructor(sent, got, status, code = '') {
    super(`${sent.split(' ').slice(0, 2).join(' ')}: ${got}`.split('\n')[0]);
    Object.assign(this, { sent, got, status, code });
  }
}
// The run cannot go on: why, and the exchange that showed it (if one did).
class Stop extends Error {
  constructor(why, exchange = null) { super(why); this.exchange = exchange; }
}
// One WebDriver command, bounded. The reply's `value`, or an Exchange.
async function command(method, path, body, boundMs = left(), { quiet = false } = {}) {
  const sent = `${method} ${path}${body === undefined ? '' : ` ${JSON.stringify(body)}`}`;
  const t0 = Date.now();
  let status = 0, text = '';
  asking = sent;
  try {
    const response = await fetch(`${base}${path}`, { method, signal: AbortSignal.timeout(boundMs),
                                                    headers: { 'Content-Type': 'application/json; charset=utf-8' },
                                                    body: body === undefined ? undefined : JSON.stringify(body) });
    status = response.status;
    text = await response.text();
  } catch (e) {
    const got = e.name === 'TimeoutError' ? `no reply in ${Date.now() - t0} ms`
                                          : `no reply (${(e.cause && e.cause.code) || e.message})`;
    if (!quiet) {
      recordExchange(sent, got);
      await sleep(1000);                                // a driver that died says so itself: its exit ends the run
    }
    throw new Exchange(sent, got, 0);
  }
  asking = '';
  if (!quiet) recordExchange(sent, `HTTP ${status} ${text}`);
  let reply = null;
  try { reply = JSON.parse(text); } catch { throw new Exchange(sent, `HTTP ${status}, and not JSON: ${text}`, status); }
  const value = reply === null || typeof reply !== 'object' ? undefined : reply.value;
  if (status !== 200 || (value && typeof value === 'object' && typeof value.error === 'string'))
    throw new Exchange(sent, `HTTP ${status} ${text}`, status, (value && value.error) || '');
  return value;
}
// A script sent as the run's time ends still has two seconds to answer: a page with no verdict is then said as that,
// and not as a driver that did not answer.
const execute = (script, boundMs = Math.min(Math.max(left(), 2000), 40000)) =>
  command('POST', `/session/${session}/execute/sync`, { script, args: [] }, boundMs);
async function shoot() {
  if (!opt.screenshot) return;
  try {
    const png = await command('GET', `/session/${session}/screenshot`, undefined, 10000);
    writeFileSync(opt.screenshot, Buffer.from(String(png), 'base64'));
    out(`page-check: screenshot: ${opt.screenshot}`);
  } catch (e) { out(`page-check: no screenshot (${e.message})`); }
}

// ---- the end --------------------------------------------------------------------------------------------------------
function flushPageLog() {
  if (pageLog.trim() !== '') out(pageLog.replace(/\n+$/, ''));
  pageLog = '';
}
// What safaridriver --diagnose wrote during this run: it prints nothing itself.
function safariLogs() {
  const dir = join(homedir(), 'Library', 'Logs', 'com.apple.WebDriver');
  try {
    for (const name of readdirSync(dir, { recursive: true })) {
      const stat = statSync(join(dir, name));
      if (stat.isFile() && stat.mtimeMs >= started - 1000)
        toDriverLog(`---- ${join(dir, name)} ----\n${readFileSync(join(dir, name), 'utf8').slice(-200000)}`, true);
    }
  } catch { /* no such directory: nothing was written */ }
}
async function finish(code, why = '', exchange = null) {
  if (ending) return;
  ending = true;
  let exit = code;
  try {
    flushPageLog();                                     // the page's log as last read, on whichever path ends the run
    if (why) err(`page-check: ${why}`);
    if (exchange) err(`  sent: ${short(exchange.sent)}\n  got:  ${short(exchange.got)}`);
    if (session && !driverGone) {
      if (code === 2) await shoot();
      try {
        await command('DELETE', `/session/${session}`, undefined, 10000);
      } catch { /* the driver is stopped below */ }
    }
    // Its group, the driver and the browser it started, whatever the driver's state: one that went away by itself left
    // its browser in the group, which outlives its leader. SIGTERM, a moment until the driver has exited and the group
    // is empty (ESRCH), SIGKILL, another moment.
    if (driver && driver.pid) {
      const signal = (s) => {
        try { process.kill(-driver.pid, s); } catch { try { driver.kill(s); } catch { /* gone */ } }
      };
      const gone = () => {
        if (driver.exitCode === null && driver.signalCode === null) return false;
        try { process.kill(-driver.pid, 0); return false; } catch (e) { return e.code === 'ESRCH'; }
      };
      const quit = async (ms) => {
        for (const until = Date.now() + ms; !gone() && Date.now() < until;) await sleep(50);
      };
      signal('SIGTERM');
      await quit(2000);
      signal('SIGKILL');
      await quit(1000);
    }
    if (server) server.close();
    for (const line of notServed) out(`page-check: server: ${line}`);
    if (repeats > 0) toDriverLog(`page-check: (the same ${repeats} more time(s))`, true);
    if (opt.browser === 'safari') safariLogs();
    toDriverLog(`page-check: exit ${code}${why ? `: ${why}` : ''}`, true);
    if (code !== 0 && driver) {
      err(`page-check: the driver's log: ${opt.driverLog}`);
      if (code === 2 && driverSaid.trim())
        err(`page-check: the driver said (its last lines):\n${driverSaid.trim().split('\n').slice(-30).join('\n')}`);
    }
    if (opt.log) writeFileSync(opt.log, `${lines.join('\n')}\n`);
  } catch (e) {
    console.error(`page-check: ${(e && e.message) || e}`);    // the end itself went wrong: never a pass
    exit = 2;
  }
  // What was printed reaches a pipe before the process goes.
  const written = (stream) => new Promise((done) => { stream.write('', () => done()); });
  await Promise.race([Promise.all([written(process.stdout), written(process.stderr)]), sleep(2000)]);
  process.exit(exit);
}
const stopped = (e) => (e instanceof Stop ? finish(2, e.message, e.exchange)
                        : e instanceof Exchange ? finish(2, 'a command failed', e)
                        : finish(2, `${(e && e.message) || e}`));
process.on('SIGINT', () => finish(2, 'interrupted'));
process.on('SIGTERM', () => finish(2, 'terminated'));
process.on('SIGHUP', () => finish(2, 'terminated'));
process.on('uncaughtException', stopped);
process.on('unhandledRejection', stopped);

// ---- reading a page -------------------------------------------------------------------------------------------------
// Every script names what it reads through `window` and `document` only: geckodriver runs it in a sandbox of its own,
// which sees the page's window but none of its top-level `let` and `const`.
const READ_VERDICT = "return [document.title, (document.getElementById('funkgui-log') || {}).textContent || ''];";
const READ_RENDERER = "var gl = document.createElement('canvas').getContext('webgl2'); if (!gl) return 'no WebGL2';"
  + "var info = gl.getExtension('WEBGL_debug_renderer_info');"
  + 'var name = String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER));'
  + "var lose = gl.getExtension('WEBGL_lose_context'); if (lose) lose.loseContext(); return name;";
// → ['frame', the text] | ['threw', why] | ['none', ''] (the module is up and has no fcmpFrame: fcmp_ui_ready sets
// the names together) | ['gone', the page's status] (the page gave up) | ['wait', the page's status].
const READ_FRAME = 'var m = window.Module, p = window.fcmpPage;'
  + "var said = (document.getElementById('fcmp-status') || {}).textContent || '';"
  + "if (m && typeof m.fcmpFrame === 'function') {"
  + "  try { return ['frame', String(m.fcmpFrame())]; } catch (e) { return ['threw', String(e)]; } }"
  + "if (m && typeof m.fcmpStatus === 'function') return ['none', ''];"
  + "var state = p && typeof p.state === 'function' ? String(p.state()) : '';"
  + "return [state === 'refused' || state === 'failed' ? 'gone' : 'wait', said];";

// Reads the page with `script` every 250 ms until done(value) says so. The value, or a Stop that names `what()`.
async function poll(script, done, what) {
  let failures = 0;
  while (Date.now() < deadline) {
    try {
      const value = await execute(script);
      failures = 0;
      if (done(value)) return value;
    } catch (e) {
      if (!(e instanceof Exchange)) throw e;
      if (e.status === 0) throw new Stop(`the driver did not answer: ${what()}`, e);
      if (/invalid session id|no such window|session deleted|disconnected/.test(`${e.code} ${e.got}`))
        throw new Stop(`the browser went away before a verdict: ${what()}`, e);
      failures += 1;                                    // a script may fail once while a page settles; not for ever
      if (failures === 3) throw new Stop(`the page could not be read: ${what()}`, e);
    }
    await sleep(Math.min(250, left()));
  }
  throw new Stop(`no verdict within ${opt.timeoutS} s: ${what()}`);
}
async function go(url, what) {
  out(`page-check: ${url}`);
  try {
    await command('POST', `/session/${session}/url`, { url }, Math.max(left(), 2000));
  } catch (e) { throw new Stop(`${what} did not load`, e); }
}
// A page of the self-test's protocol: its title once it is a verdict. The log is printed as it stood then.
const isVerdict = (title) => title === 'PASS' || title.startsWith('FAIL');
async function verdictOf(url, what) {
  const t0 = Date.now();
  await go(url, what);
  let title = '';
  await poll(READ_VERDICT, (value) => {
    if (Array.isArray(value)) [title, pageLog] = value.map(String);
    return isVerdict(title);
  }, () => `${what} (the title was '${title}')`);
  flushPageLog();
  return { title, ms: Date.now() - t0 };
}

// A frame against its expectation, as the gate compares: every line but `live` and `hooks`, each `name value`.
function compareFrame(text, wantPath) {
  const rows = (t) => new Map(t.split('\n').filter((l) => l !== '').map((l) => {
    const i = l.indexOf(' ');
    return i < 0 ? [l, ''] : [l.slice(0, i), l.slice(i + 1)];
  }));
  const fail = (why) => ({ status: 'FAIL', why, detail: '', differ: [] });
  if (text.trim() === '') return fail('Module.fcmpFrame() returned nothing');
  if (!existsSync(wantPath)) return fail(`no expectation (${wantPath})`);
  const live = rows(text);
  const want = rows(readFileSync(wantPath, 'utf8'));
  if (want.size === 0) return fail(`the expectation is empty (${wantPath})`);
  const hooks = live.get('hooks') || '';
  if (!/^dpi 2 clock fixed theme \d+ dt \S+ settle \d+ drawn 1 idle 1$/.test(hooks))
    return fail(`the hooks line is '${hooks}' (want dpi 2 clock fixed ... drawn 1 idle 1)`);
  const differ = [];
  let compared = 0;
  for (const key of new Set([...want.keys(), ...live.keys()])) {
    if (key === 'hooks' || key === 'live') continue;
    compared += 1;
    if (!live.has(key)) differ.push(`${key}: not in the browser's frame, node ${want.get(key)}`);
    else if (!want.has(key)) differ.push(`${key}: browser ${live.get(key)}, not in the expectation`);
    else if (live.get(key) !== want.get(key)) differ.push(`${key}: browser ${live.get(key)}, node ${want.get(key)}`);
  }
  const detail = `geometry ${live.get('geometry')} text ${live.get('text')}; ${hooks}`;
  if (differ.length === 0) return { status: 'EQUAL', why: `${compared} lines equal`, detail, differ };
  return { status: 'DIFFERS', why: `${differ.length} of ${compared} lines differ`, detail, differ };
}

// ---- the run --------------------------------------------------------------------------------------------------------
async function main() {
  writeFileSync(opt.driverLog, `page-check: ${new Date(started).toISOString()} ${opt.browser}: every command sent (>), `
                               + 'every reply (<) and what the driver printed\n');
  server = await serve([['/live/', opt.live], ['/expect/', opt.expect], ['/', opt.site]]);
  const origin = `http://127.0.0.1:${server.address().port}`;

  // The driver: its version first, then the driver itself on a free port.
  const port = await freePort();
  const { path, exe, args, versionArgs } = driverCommand(opt, port);
  const version = await new Promise((done) => {
    execFile(exe, versionArgs, { timeout: 10000 }, (error, stdout, stderr) => {
      const first = `${stdout}${stderr}`.trim().split('\n')[0];
      const why = error ? `${error.code ?? error.signal}${first ? `, ${first}` : ''}` : '';
      done(!error ? first : error.code === 'ENOENT' ? null : `version unknown (--version: ${why})`);
    });
  });
  if (version === null)
    throw new Stop(`${path} did not start (ENOENT): no such driver${opt.driver ? '' : '; --driver <path> names one'}`);
  out(`page-check: driver: ${version} (${[exe, ...args].join(' ')}); its log: ${opt.driverLog}`);
  driver = spawn(exe, args, { stdio: ['ignore', 'pipe', 'pipe'], detached: process.platform !== 'win32' });
  const hear = (d) => { driverSaid = `${driverSaid}${d}`.slice(-65536); toDriverLog(String(d)); };
  driver.stdout.on('data', hear);
  driver.stderr.on('data', hear);
  driver.on('error', (e) => { driverGone = e.code || e.message; finish(2, `${path} did not start (${driverGone})`); });
  driver.on('exit', (code, signal) => {
    driverGone = signal || `code ${code}`;
    finish(2, `the driver went away (${driverGone})${asking ? ` while it was asked: ${short(asking, 300)}` : ''}`);
  });
  // Ready: /status answers, on whichever loopback name the driver listens. Only `ready: false` means not yet.
  let ready = false, refused = null;
  const until = Math.min(deadline, Date.now() + 30000);
  while (!ready && Date.now() < until && !ending) {
    for (const host of ['127.0.0.1', 'localhost']) {
      base = `http://${host}:${port}`;
      try {
        const status = await command('GET', '/status', undefined, 1000, { quiet: true });
        ready = !(status && status.ready === false);
        refused = ready ? null : new Exchange('GET /status', `HTTP 200 ${JSON.stringify({ value: status })}`, 200);
      } catch (e) {
        ready = e.status > 0;                           // an answer, if not this command's: New Session will tell
        refused = e;
      }
      if (ready) break;
    }
    if (!ready) await sleep(100);
  }
  if (ending) return;
  if (!ready) throw new Stop(`${path} was not ready in time (its /status never said ready)`, refused);

  // The session: what the browser is asked for, and what it says it is.
  const wanted = capabilities(opt);
  let made = null;
  try {
    made = await command('POST', '/session', { capabilities: { alwaysMatch: wanted } });
  } catch (e) { throw new Stop('no session', e); }
  if (!made || typeof made.sessionId !== 'string' || made.sessionId === '')
    throw new Stop('no session: the reply names no sessionId',
                   new Exchange(`POST /session ${JSON.stringify({ capabilities: { alwaysMatch: wanted } })}`,
                                `HTTP 200 ${JSON.stringify({ value: made })}`, 200));
  session = made.sessionId;
  const got = made.capabilities || {};
  out(`page-check: browser: ${got.browserName || opt.browser} ${got.browserVersion || '(no version given)'} on `
      + `${got.platformName || process.platform}; asked for ${JSON.stringify(wanted)}; it answered `
      + `${short(JSON.stringify(got), 1200)}`);
  if (opt.autoplay && opt.browser === 'safari') out('page-check: --autoplay does nothing for Safari');
  // In a command of its own, not among the capabilities: a driver that refuses it still gives a session.
  try {
    await command('POST', `/session/${session}/timeouts`, { script: 30000, pageLoad: 60000 });
  } catch (e) { out(`page-check: the timeouts were not set (${e.message})`); }

  const parts = [];                                     // { name, ok, why }
  const judge = (status, name, why, ms, detail = '', differ = []) => {
    out(`${status.padEnd(8)} ${name}: ${why}${detail ? `; ${detail}` : ''} (${ms} ms)`);
    for (const line of differ.slice(0, 12)) out(`         ${line}`);
    if (differ.length > 12) out(`         and ${differ.length - 12} more`);
    parts.push({ name, ok: status === 'PASS' || status === 'EQUAL', why });
  };
  const rowOf = (title) =>
    (title === 'PASS' ? 'the page says PASS' : title.replace(/^FAIL:?\s*/, '') || 'no row is named');

  // The self-test.
  {
    const { title, ms } = await verdictOf(`${origin}/index.html?selftest=1`, 'the self-test');
    try {
      out(`page-check: renderer: ${await execute(READ_RENDERER, 5000)}`);     // what drew the canvas, for the record
    } catch (e) { out(`page-check: renderer: unknown (${e.message})`); }
    await shoot();
    judge(title === 'PASS' ? 'PASS' : 'FAIL', 'selftest', rowOf(title), ms);
  }

  // The capture pages: the settled frame against the node value.
  for (const view of opt.frames ? VIEWS : []) {
    const name = `${view}.theme0`;
    const t0 = Date.now();
    await go(`${origin}/index.html?view=${view}&${PINS}`, `the capture page ${name}`);
    let said = '';
    const [kind, text] = await poll(READ_FRAME, (value) => {
      if (Array.isArray(value)) said = String(value[1]);
      return Array.isArray(value) && value[0] !== 'wait';
    }, () => `the capture page ${name} (the editor is not up; the page says '${said}')`);
    const ms = Date.now() - t0;
    if (kind === 'frame') {
      const { status, why, detail, differ } = compareFrame(String(text), join(opt.expect, `${name}.node.fp`));
      judge(status, name, why, ms, detail, differ);
    } else {
      judge('FAIL', name, kind === 'none' ? 'the module has no Module.fcmpFrame'
                        : kind === 'threw' ? `Module.fcmpFrame() threw: ${text}`
                        : `the editor did not come up (the page says '${text}')`, ms);
    }
  }

  // The live pages.
  const pages = opt.pages ? readdirSync(opt.live).filter((n) => n.endsWith('.html')).sort() : [];
  if (opt.pages && pages.length === 0) out(`NOTE     ${opt.live} has no page`);
  for (const page of pages) {
    const { title, ms } = await verdictOf(`${origin}/live/${encodeURIComponent(page)}`, `live/${page}`);
    judge(title === 'PASS' ? 'PASS' : 'FAIL', `live/${page}`, rowOf(title), ms);
  }

  const failed = parts.filter((p) => !p.ok);
  out(`page-check: ${parts.length - failed.length}/${parts.length} passed`);
  out(failed.length === 0 ? 'page-check: PASS' : `page-check: FAIL: ${failed[0].name}: ${failed[0].why}`);
  await finish(failed.length === 0 ? 0 : 1);
}

main().catch(stopped);
