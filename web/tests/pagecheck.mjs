// web/tests/pagecheck.mjs: Scripts/web/page-check.mjs, the WebDriver runner of CI's browser legs, against a fake
// driver (ADR-93, the lead phase). No browser and no real driver is started.
//
// FCMP_WEB_TEST name=web.pagecheck timeout=180 args={source}
//
//   node pagecheck.mjs <repository> [<runner>]      (<runner>: another copy of page-check.mjs, for a mutation)
//
// The runner is run as CI runs it, a child process, with support/fake-driver.mjs as its driver: a WebDriver server
// that plays one scenario, runs the runner's scripts against a model of the page and records what it was asked. The
// fake starts a process of its own in the browser's place, in its process group, and leaves it there: a run's driver
// is gone when the fake and that process both are. The site, the live directory and the expectations are scratch
// files; nothing here needs a build.
//   pass.*, fail.*, timeout.*    the three verdicts and their exit codes; the log, the versions first, the files
//   server.*                     the runner's server: the three roots, types, HEAD, no caching, nothing above a root
//   hang, nosession, crash, ...  what can go wrong on the way: exit 2, one message that says what was sent and what
//                                came back and names the driver's log; the session deleted and the driver gone
//   cleanup.*, protocol.*        every run: the driver and its browser gone; no request a real driver would refuse
//   frames.*                     the six capture pages against the expectations, as the gate compares them
//   pages.*                      the live pages by the live-page protocol
//   caps.*, driver.*             what each browser and each driver is asked for; the Safari refusal; where a driver
//                                is looked for
//   files.*                      the directories of --log, --driver-log and --screenshot
//   usage.*                      the command line
// The short bounds given to the runs that must time out are multiplied by FCMP_TIMING_SCALE (a loaded machine).
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { spawn } from 'node:child_process';
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { delimiter, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const TEST = 'web.pagecheck';

let passed = 0;
let failed = 0;
function row(ok, name, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}     ${TEST} ${name}${detail ? ': ' + detail : ''}`);
  if (ok) passed += 1;
  else failed += 1;
  return ok;
}

const [source, other] = process.argv.slice(2);
if (!source) {
  console.error('usage: node pagecheck.mjs <repository> [<runner>]');
  process.exit(2);
}
const runner = other || join(source, 'Scripts', 'web', 'page-check.mjs');
const fake = join(dirname(fileURLToPath(import.meta.url)), 'support', 'fake-driver.mjs');
if (!existsSync(runner) || !existsSync(fake)) {
  row(false, 'inputs', `no ${runner} or no ${fake}`);
  process.exit(1);
}

// ---- the scratch site, live directory and expectations --------------------------------------------------------------
const VIEWS = ['panel', 'chars.sidechain', 'chars.colour', 'modebrowser', 'presetbrowser', 'settings'];
const PINS = 'theme=0&zoom=100&scale=2&dt=0.0166666675&nohint=1&nolive=1&host=WEB-LIVE';
const scratch = mkdtempSync(join(tmpdir(), 'fcmp-pagecheck-'));
const site = join(scratch, 'site');
const live = join(scratch, 'live');
const expect = join(scratch, 'expect');
const fewer = join(scratch, 'expect-fewer');            // the expectations without settings'
const bare = join(scratch, 'live-bare');                // a live directory with no page
const home = join(scratch, 'home');                     // every run's HOME: the real one is neither read nor written
const bin = join(scratch, 'bin');                       // the fake under each driver's own name
for (const dir of [join(site, 'licences'), live, expect, fewer, bare, home, bin, join(scratch, 'tmp'),
                   join(scratch, 'nosite')])
  mkdirSync(dir, { recursive: true });
for (const name of ['chromedriver', 'geckodriver']) {
  writeFileSync(join(bin, name), `#!/bin/sh\nexec '${process.execPath}' '${fake}' "$@"\n`);
  chmodSync(join(bin, name), 0o755);
}
writeFileSync(join(site, 'index.html'), '<!doctype html><title>RUNNING</title>');
writeFileSync(join(site, 'main.js'), 'export {};');
writeFileSync(join(site, 'fcmp-engine.wasm'), Buffer.from([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0]));
writeFileSync(join(scratch, 'secret.txt'), 'not the site');
for (const name of ['a.html', 'b.html']) writeFileSync(join(live, name), '<!doctype html><title>RUNNING</title>');
writeFileSync(join(live, 'fcmp-print.wasm'), Buffer.from([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0]));
writeFileSync(join(live, 'notes.txt'), 'not a page');
writeFileSync(join(bare, 'notes.txt'), 'not a page');
VIEWS.forEach((view, i) => {
  const text = `geometry 738cee5019a4f33${i}\ntext 3e535f56470579c${i}\nstatics 120\nlive 14\ntexts 60\nmax_x 960\n`
             + 'tag.knob 12\ntag.meter 2\nview_w 960\nview_h 640\nglyphs_missing 0\n';
  writeFileSync(join(expect, `${view}.theme0.node.fp`), text);
  if (view !== 'settings') writeFileSync(join(fewer, `${view}.theme0.node.fp`), text);
});

// ---- runs of the runner ---------------------------------------------------------------------------------------------
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const scale = Math.max(1, Number(process.env.FCMP_TIMING_SCALE) || 1);
const seconds = (s) => String(Math.round(s * scale));       // a --timeout that the run is meant to reach
// A process that is still there. A zombie is not (Linux: one whose parent has not reaped it yet still answers kill 0).
const alive = (pid) => {
  try { process.kill(pid, 0); } catch { return false; }
  try { return !/^\d+ \(.*\) Z/s.test(readFileSync(`/proc/${pid}/stat`, 'utf8')); } catch { return true; }
};
const readRecord = (path) => { try { return JSON.parse(readFileSync(path, 'utf8')); } catch { return null; } };
const textOf = (path) => { try { return readFileSync(path, 'utf8'); } catch { return ''; } };
let runs = 0;
let running = 0;
const queue = [];
// One run of the runner, at most six at a time. → { code, out, err, ms, record, driverLog, left, gone }
// signalWhen(record): `signal` to the runner once the fake's record says so.
async function run(scenario, args, { env = {}, signalWhen = null, signal = 'SIGTERM', ownDriverLog = true } = {}) {
  runs += 1;
  const n = runs;
  if (running >= 6) await new Promise((free) => queue.push(free));
  running += 1;
  const recordPath = join(scratch, `record-${n}.json`);
  const driverLogPath = join(scratch, `driver-${n}.log`);
  const t0 = Date.now();
  const result = await new Promise((done) => {
    const child = spawn(process.execPath, [runner, ...args, ...(ownDriverLog ? ['--driver-log', driverLogPath] : [])],
                        { env: { ...process.env, GITHUB_ACTIONS: '', CHROMEWEBDRIVER: '', GECKOWEBDRIVER: '',
                                 HOME: home, FAKE_SCENARIO: scenario, FAKE_RECORD: recordPath, ...env },
                          stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', err = '';
    child.stdout.on('data', (d) => { out += d; });
    child.stderr.on('data', (d) => { err += d; });
    let watch = null;
    if (signalWhen) {
      watch = setInterval(() => {
        const record = readRecord(recordPath);
        if (record && signalWhen(record)) { clearInterval(watch); child.kill(signal); }
      }, 50);
    }
    child.on('exit', (code) => { clearInterval(watch); done({ code, out, err, ms: Date.now() - t0 }); });
  });
  result.record = readRecord(recordPath);
  result.driverLog = textOf(driverLogPath);
  // The driver and its browser are gone: looked at for a moment, since a killed process takes one to leave the table.
  // What is left is named, and killed here, so that a run that failed leaves nothing behind.
  const pids = result.record === null ? {} : { driver: result.record.pid, browser: result.record.browser };
  for (let i = 0; i < 60 && Object.values(pids).some(alive); i += 1) await sleep(50);
  result.left = Object.keys(pids).filter((k) => alive(pids[k]));
  for (const k of result.left) { try { process.kill(pids[k], 'SIGKILL'); } catch { /* gone */ } }
  result.gone = result.record !== null && Number.isInteger(result.record.browser) && result.left.length === 0;
  running -= 1;
  if (queue.length > 0) queue.shift()();
  return result;
}
const chrome = [site, '--browser', 'chrome', '--driver', fake];
const firefox = [site, '--browser', 'firefox', '--driver', fake];
const all = [...chrome, '--live', live, '--expect', expect, '--frames', '--pages'];
const frames = [...chrome, '--expect', expect, '--frames'];
const pages = [...chrome, '--live', live, '--pages'];
const count = (text, pattern) => (text.match(pattern) || []).length;
const said = (r) => `exit ${r.code} after ${r.ms} ms`;
const capsOf = (r) => (r.record && r.record.capabilities) || {};   // what New Session was asked for, if it was
// What the runner's server answered the fake (server.* below); an answer never given is status 0.
const unanswered = { status: 0, type: '', bytes: 0, cache: '', secret: false };
const answers = (record) => new Proxy(record ? record.served : {}, { get: (o, k) => o[k] || unanswered });
const leftOf = (r) => (r.gone ? 'the driver and its browser gone' : r.record === null ? 'no record'
                              : `still there: ${r.left.join(', ') || 'none, but the fake named no browser'}`);
const shot = join(scratch, 'pass.png');
const logFile = join(scratch, 'pass.txt');
const lateShot = join(scratch, 'running.png');
const made = join(scratch, 'made');                     // not there: the runner makes what its files need

// Every run is started here and judged below, so that six go at a time.
const R = {
  pass: run('pass', [...chrome, '--live', live, '--expect', expect, '--screenshot', shot, '--log', logFile]),
  fail: run('fail', [...chrome, '--log', join(scratch, 'fail.txt')]),
  running: run('running', [...chrome, '--timeout', seconds(6), '--screenshot', lateShot]),
  hang: run('hang', [...chrome, '--timeout', seconds(6)]),
  nosession: run('nosession', chrome),
  noid: run('noid', chrome),
  crash: run('crash', [...chrome, '--timeout', '90']),
  lost: run('lost', [...chrome, '--timeout', '90']),
  notready: run('notready', [...chrome, '--timeout', seconds(5)]),
  silent: run('silent', [...chrome, '--timeout', seconds(5)]),
  nostatus: run('nostatus', chrome),
  garbage: run('garbage', [...chrome, '--timeout', '90']),
  scripterror: run('scripterror', [...chrome, '--timeout', '90']),
  flaky: run('flaky', all),
  noshot: run('noshot', [...chrome, '--screenshot', join(scratch, 'none.png')]),
  nolog: run('pass', [...chrome, '--log', join(scratch, 'tmp')]),
  newDirs: run('pass', [...chrome, '--log', join(made, 'a', 'log.txt'), '--screenshot', join(made, 'b', 'c', 'p.png'),
                        '--driver-log', join(made, 'd', 'driver.log')], { ownDriverLog: false }),
  noDir: run('pass', [...chrome, '--log', join(scratch, 'secret.txt', 'log.txt')]),
  nodriver: run('pass', [site, '--browser', 'chrome', '--driver', join(scratch, 'no-such-driver')]),
  noversion: run('noversion', chrome),
  notimeouts: run('notimeouts', chrome),
  slowquit: run('slowquit', chrome),
  sigterm: run('running', [...chrome, '--timeout', '90'], { signalWhen: (record) => record.polls >= 2 }),
  sigint: run('running', [...chrome, '--timeout', '90'], { signalWhen: (record) => record.polls >= 2,
                                                           signal: 'SIGINT' }),
  defaultLog: run('pass', chrome, { env: { TMPDIR: join(scratch, 'tmp') }, ownDriverLog: false }),
  all: run('pass', all),
  framediff: run('framediff', all),
  framebad: run('framebad', frames),
  frameless: run('frameless', [...frames, '--timeout', '40']),
  framegone: run('framegone', [...frames, '--timeout', '40']),
  framefewer: run('pass', [...chrome, '--expect', fewer, '--frames']),
  failframes: run('fail', frames),
  livefail: run('livefail', pages),
  livehang: run('livehang', [...pages, '--timeout', seconds(8)]),
  livebare: run('pass', [...chrome, '--live', bare, '--pages']),
  capsChrome: run('pass', [...chrome, '--autoplay', '--arg', '--use-angle=gl', '--binary', '/opt/x/chrome']),
  capsHeaded: run('pass', [...chrome, '--headed']),
  capsFirefox: run('pass', [...firefox, '--autoplay', '--binary', '/opt/x/firefox']),
  capsFirefoxDefault: run('pass', firefox),
  safariRefused: run('pass', [site, '--browser', 'safari', '--driver', fake]),
  safari: run('pass', [site, '--browser', 'safari', '--driver', fake, '--autoplay'],
              { env: { GITHUB_ACTIONS: 'true', FAKE_HOME: home } }),
  safariHere: run('pass', [site, '--browser', 'safari', '--driver', fake, '--safari-here']),
  driverEnvChrome: run('pass', [site, '--browser', 'chrome'], { env: { CHROMEWEBDRIVER: bin } }),
  driverEnvGecko: run('pass', [site, '--browser', 'firefox'], { env: { GECKOWEBDRIVER: bin } }),
  driverPath: run('pass', [site, '--browser', 'firefox'], { env: { PATH: `${bin}${delimiter}${process.env.PATH}` } }),
  usageNoBrowser: run('pass', [site, '--driver', fake]),
  usageBrowser: run('pass', [site, '--browser', 'edge', '--driver', fake]),
  usageOption: run('pass', [...chrome, '--page', 'index.html']),
  usageTwoSites: run('pass', [...chrome, live]),
  usageTimeout: run('pass', [...chrome, '--timeout', '0']),
  usageValue: run('pass', [...chrome, '--log'], { ownDriverLog: false }),
  usageFrames: run('pass', [...chrome, '--frames']),
  usagePages: run('pass', [...chrome, '--pages']),
  noIndex: run('pass', [join(scratch, 'nosite'), '--browser', 'chrome', '--driver', fake]),
  noLive: run('pass', [...chrome, '--live', join(scratch, 'no-such-directory'), '--pages']),
};
for (const key of Object.keys(R)) R[key] = await R[key];

// ---- the three verdicts ---------------------------------------------------------------------------------------------
{
  const r = R.pass;
  const o = r.out.split('\n');
  row(r.code === 0, 'pass.exit', said(r));
  row(/^PASS {5}web\.selftest browser/m.test(r.out) && /^PASS {5}selftest: the page says PASS \(\d+ ms\)$/m.test(r.out)
      && /^page-check: 1\/1 passed\npage-check: PASS$/m.test(r.out), 'pass.log',
      'the page\'s log, the part\'s line, the count and the verdict are printed');
  row(/^page-check: driver: fake-driver 1\.0 \(a test double\) \(.*fake-driver\.mjs --port=\d+ --verbose\); its log: /
        .test(o[0])
      && /^page-check: browser: chrome 1\.2\.3 on fake; asked for \{"browserName":"chrome".*; it answered \{/.test(o[1])
      && /"fake:driverVersion":"1\.0"/.test(o[1])
      && /^page-check: http:\/\/127\.0\.0\.1:\d+\/index\.html\?selftest=1$/.test(o[2]), 'pass.versions',
      'the driver\'s version and command line, then the browser\'s answer, before the page is opened');
  row(r.record.deleted === 1 && r.gone, 'pass.cleanup', `sessions deleted ${r.record.deleted}, ${leftOf(r)}`);
  row(existsSync(shot) && readFileSync(shot).subarray(1, 4).toString() === 'PNG' && r.record.screenshots === 1,
      'pass.screenshot', `${r.record.screenshots} picture(s) asked for`);
  row(textOf(logFile) === r.out, 'pass.logfile', '--log holds what was printed');
  row(/renderer: Fake Renderer \(software\)/.test(r.out) && r.record.lostContexts === 1, 'pass.renderer',
      'the WebGL renderer is named, and its throwaway context given back');
  row(r.record.urls.length === 1 && /^http:\/\/127\.0\.0\.1:\d+\/index\.html\?selftest=1$/.test(r.record.urls[0]),
      'pass.url', r.record.urls.join(' '));
  row(JSON.stringify(r.record.timeouts) === '{"script":30000,"pageLoad":60000}', 'pass.timeouts',
      `Set Timeouts: ${JSON.stringify(r.record.timeouts)}`);
  const d = r.driverLog;
  row(/> POST \/session \{"capabilities":\{"alwaysMatch":\{"browserName":"chrome"/.test(d)
      && /< HTTP 200 \{"value":\{"sessionId":"fake-session-1"/.test(d)
      && /^fake-driver: scenario pass on port \d+$/m.test(d)
      && /\(the same \d+ more time\(s\)\)/.test(d) && /> DELETE \/session\/fake-session-1\n.*< HTTP 200/.test(d)
      && /^page-check: exit 0$/m.test(d), 'pass.driverlog',
      'the driver\'s log holds each command, each reply, the driver\'s own output and the exit');

  // The runner's own server, as the fake asked it on the first Navigate.
  const s = answers(r.record);
  row(s.page.status === 200 && s.page.type.startsWith('text/html'), 'server.page', JSON.stringify(s.page));
  row(s.wasm.status === 200 && s.wasm.type === 'application/wasm' && s.wasm.bytes === 8, 'server.wasm',
      JSON.stringify(s.wasm));
  row(s.script.status === 200 && s.script.type.startsWith('text/javascript'), 'server.script',
      JSON.stringify(s.script));
  row(s.head.status === 200 && s.head.bytes === 0, 'server.head', JSON.stringify(s.head));
  row(s.missing.status === 404 && /^page-check: server: 404 GET \/no-such-file\.js$/m.test(r.out), 'server.missing',
      'a 404 is answered and said at the end');
  const caches = [...new Set(Object.values(s).map((a) => a.cache))];
  row(caches.join() === 'no-store', 'server.nocache',
      `Cache-Control of ${Object.keys(s).length} answers: ${caches.join(', ')}`);
  const above = ['above', 'aboveEncoded', 'aboveSlash', 'liveAbove', 'liveSibling', 'expectAbove'];
  row(above.every((k) => s[k].status > 0 && s[k].status !== 200 && !s[k].secret), 'server.above',
      `nothing above a root is served: ${above.map((k) => `${k} ${s[k].status}`).join(', ')}`);
  row(s.malformed.status === 400 && s.nul.status === 404, 'server.malformed',
      `a path that does not decode ${s.malformed.status}, one with a NUL ${s.nul.status}`);
  row(s.post.status === 405, 'server.post', `POST ${s.post.status}`);
  row(s.directory.status === 200 && s.directory.type.startsWith('text/html'), 'server.directory', '/ is index.html');
  row(s.live.status === 200 && s.live.type.startsWith('text/html') && s.liveModule.status === 200
      && s.liveModule.type === 'application/wasm', 'server.live', `/live/ is --live: ${JSON.stringify(s.live)}`);
  row(s.liveUp.status === 200 && s.liveUp.type.startsWith('text/javascript'), 'server.live.up',
      `a live page reaches the site as ../<name>: ${JSON.stringify(s.liveUp)}`);
  row(s.expect.status === 200 && s.expect.bytes > 100, 'server.expect',
      `/expect/ is --expect: ${JSON.stringify(s.expect)}`);
  const n = answers(R.fail.record);
  row(n.live.status === 404 && n.expect.status === 404, 'server.roots.absent',
      `with no --live and no --expect: /live/a.html ${n.live.status}, /expect/panel.theme0.node.fp ${n.expect.status}`);
}
{
  const r = R.fail;
  row(r.code === 1 && /^FAIL {5}web\.selftest editor\.pixels/m.test(r.out)
      && /^FAIL {5}selftest: editor\.pixels \(/m.test(r.out)
      && /^page-check: 0\/1 passed\npage-check: FAIL: selftest: editor\.pixels$/m.test(r.out), 'fail.exit', said(r));
  row(r.record.deleted === 1 && r.gone, 'fail.cleanup', leftOf(r));
  row(/^page-check: the driver's log: .*driver-\d+\.log$/m.test(r.err) && !/the driver said/.test(r.err)
      && textOf(join(scratch, 'fail.txt')).includes('the driver\'s log:'), 'fail.driverlog',
      'a FAIL names the driver\'s log, on stderr and in --log');
}
{
  const r = R.running;
  row(r.code === 2 && r.err.includes(`no verdict within ${seconds(6)} s: the self-test (the title was 'RUNNING')`),
      'timeout.exit', said(r));
  row(/^NOTE {5}a fake browser$/m.test(r.out) && !/passed/.test(r.out), 'timeout.log',
      'the log as last read is printed');
  row(r.record.deleted === 1 && r.gone && existsSync(lateShot), 'timeout.cleanup',
      `session deleted, a picture taken; ${leftOf(r)}`);
}
// ---- what can go wrong on the way -----------------------------------------------------------------------------------
{
  const r = R.hang;
  row(r.code === 2 && /the driver did not answer: the self-test/.test(r.err)
      && /^ {2}sent: POST \/session\/fake-session-1\/execute\/sync \{"script":"return \[document\.title/m.test(r.err)
      && /^ {2}got: {2}no reply in \d+ ms$/m.test(r.err), 'hang.exit',
      `${said(r)}; what was sent and that nothing came back`);
  row(r.record.deleted === 1 && r.gone, 'hang.cleanup', leftOf(r));
}
{
  const r = R.nosession;
  row(r.code === 2 && /^page-check: no session$/m.test(r.err)
      && /^ {2}sent: POST \/session \{"capabilities":\{"alwaysMatch":\{"browserName":"chrome","goog:chromeOptions"/m
        .test(r.err)
      && /^ {2}got: {2}HTTP 500 \{"value":\{"error":"session not created","message":"session not created: the browser/m
        .test(r.err),
      'nosession.exit', `${said(r)}; what was sent and what came back`);
  row(/the driver's log: .*driver-\d+\.log\n/.test(r.err)
      && /the driver said \(its last lines\):\nfake-driver: scenario nosession/.test(r.err)
      && r.gone && r.record.deleted === 0, 'nosession.said', 'the driver\'s log is named and its output shown');
  const i = R.noid;
  row(i.code === 2 && /^page-check: no session: the reply names no sessionId$/m.test(i.err)
      && /^ {2}got: {2}HTTP 200 \{"value":\{"capabilities":/m.test(i.err) && i.gone && i.record.urls.length === 0,
      'noid.exit', `a reply with no sessionId is not a session (${said(i)})`);
}
{
  const r = R.crash;
  row(r.code === 2 && r.ms < 45000
      && /the driver went away \(code 7\) while it was asked: POST \/session\/\S+\/execute\/sync /.test(r.err),
      'crash.exit', said(r));
  row(count(r.err, /^page-check: (?!the driver's log|the driver said)/mg) === 1, 'crash.once',
      'one message: the driver\'s exit, not the failed command as well');
  row(/^NOTE {5}a fake browser$/m.test(r.out), 'crash.log', 'the log as last read is printed');
  row(r.record.deleted === 0 && r.gone, 'crash.cleanup',
      `a driver that went away first leaves its browser in its group, and the group is killed: ${leftOf(r)}`);
}
{
  const r = R.lost;
  row(r.code === 2 && /the browser went away before a verdict: the self-test/.test(r.err)
      && /got: {2}HTTP 404 \{"value":\{"error":"invalid session id"/.test(r.err) && r.ms < 45000, 'lost.exit', said(r));
  row(r.gone, 'lost.cleanup', leftOf(r));
}
{
  const r = R.notready;
  row(r.code === 2 && /was not ready in time/.test(r.err)
      && /sent: GET \/status\n {2}got: {2}HTTP 200 \{"value":\{"ready":false/.test(r.err)
      && r.gone && r.record.capabilities === null, 'notready.exit', `${said(r)}; no session was asked for`);
  const s = R.silent;
  row(s.code === 2 && /was not ready in time/.test(s.err) && /sent: GET \/status\n {2}got: {2}no reply/.test(s.err)
      && s.gone && s.record.capabilities === null, 'silent.exit', said(s));
  const n = R.nostatus;
  row(n.code === 0 && n.record.deleted === 1, 'nostatus.exit',
      `a driver whose /status is an unknown command is still used (${said(n)})`);
}
{
  const r = R.garbage;
  row(r.code === 2 && /the page could not be read: the self-test/.test(r.err)
      && /got: {2}HTTP 200, and not JSON: <html>not json/.test(r.err) && r.gone && r.ms < 45000, 'garbage.exit',
      said(r));
  const e = R.scripterror;
  row(e.code === 2 && /the page could not be read/.test(e.err) && /"error":"javascript error"/.test(e.err)
      && e.record.deleted === 1 && e.record.polls === 3, 'scripterror.exit',
      `${said(e)} at the third failure in a row`);
  const f = R.flaky;
  row(f.code === 0 && /^page-check: 9\/9 passed$/m.test(f.out), 'flaky.exit',
      `a script that fails once on each page does not end the run (${said(f)})`);
}
{
  const r = R.noshot;
  row(r.code === 0 && !existsSync(join(scratch, 'none.png'))
      && /no screenshot \(GET \/session\/\S+\/screenshot: HTTP 500 .*unable to capture screen/.test(r.out),
      'noshot.exit', 'a picture that cannot be taken does not change the verdict');
  const l = R.nolog;
  row(l.code === 2 && /^page-check: EISDIR/m.test(l.err) && l.record.deleted === 1 && l.gone, 'nolog.exit',
      `a log that cannot be written is not a pass (${said(l)})`);
  const m = R.newDirs;
  row(m.code === 0 && existsSync(join(made, 'a', 'log.txt')) && existsSync(join(made, 'b', 'c', 'p.png'))
      && /> POST \/session /.test(textOf(join(made, 'd', 'driver.log'))), 'files.directories',
      'the directories of --log, --screenshot and --driver-log are made');
  const n = R.noDir;
  row(n.code === 2 && n.record === null && /^page-check: .*secret\.txt.log\.txt: /m.test(n.err), 'files.refused',
      'a file whose directory cannot be made is refused before anything is started');
  const d = R.nodriver;
  row(d.code === 2 && /no-such-driver did not start \(ENOENT\)/.test(d.err), 'nodriver.exit', said(d));
  const v = R.noversion;
  row(v.code === 0
      && /^page-check: driver: version unknown \(--version: 3, fake-driver: unknown option --version\)/.test(v.out),
      'noversion.exit', 'a driver that does not tell its version is still used');
  const t = R.notimeouts;
  row(t.code === 0 && t.record.timeouts === null
      && /^page-check: the timeouts were not set \(POST \/session\/\S+\/timeouts: HTTP 400/m.test(t.out),
      'notimeouts.exit', 'a driver that refuses Set Timeouts is still used');
}
{
  const r = R.slowquit;
  row(r.code === 0 && r.record.sigterm === true && r.gone, 'slowquit.killed',
      `a driver and a browser that ignore SIGTERM are killed (${said(r)}; ${leftOf(r)})`);
  const t = R.sigterm;
  row(t.code === 2 && /terminated/.test(t.err) && t.record.deleted === 1 && t.gone, 'sigterm.cleanup',
      `the runner told to stop ends the session, the driver and its browser (${said(t)}; ${leftOf(t)})`);
  const i = R.sigint;
  row(i.code === 2 && /^page-check: interrupted$/m.test(i.err) && i.record.deleted === 1 && i.gone, 'sigint.cleanup',
      `the runner interrupted ends the session, the driver and its browser (${said(i)}; ${leftOf(i)})`);
  const d = R.defaultLog;
  const path = join(scratch, 'tmp', 'page-check-chrome-driver.log');
  row(d.code === 0 && /> POST \/session /.test(textOf(path))
      && d.out.includes(`its log: ${path}`),
      'driverlog.default', 'with no --driver-log the log is written under the temporary directory, and named');
}
// ---- the capture pages ----------------------------------------------------------------------------------------------
{
  const r = R.all;
  const equal = VIEWS.filter((v, i) => new RegExp(
    `^EQUAL {4}${v.replace('.', '\\.')}\\.theme0: 10 lines equal; geometry 738cee5019a4f33${i} text 3e535f56470579c${i}`
    + '; dpi 2 clock fixed theme 0 dt 0\\.0166666675 settle 28 drawn 1 idle 1 \\(\\d+ ms\\)$', 'm').test(r.out));
  row(r.code === 0 && equal.length === 6, 'frames.equal',
      `${equal.length} of 6 views EQUAL though the browser's \`live\` count is not node's (${said(r)})`);
  const urls = r.record.urls.map((u) => u.replace(/^http:\/\/127\.0\.0\.1:\d+/, ''));
  const want = ['/index.html?selftest=1', ...VIEWS.map((v) => `/index.html?view=${v}&${PINS}`), '/live/a.html',
                '/live/b.html'];
  row(JSON.stringify(urls) === JSON.stringify(want), 'frames.urls',
      `the self-test, the contract's six capture pages, the live pages in order: ${urls.length} pages`);
  row(/^PASS {5}web\.live\.fake a\.html rows\.count: 112$/m.test(r.out)
      && /^PASS {5}live\/a\.html: the page says PASS \(/m.test(r.out)
      && /^PASS {5}live\/b\.html: the page says PASS \(/m.test(r.out)
      && /^page-check: 9\/9 passed\npage-check: PASS$/m.test(r.out),
      'pages.pass', 'each live page\'s log and verdict, and the count');
  row(r.record.screenshots === 0 && r.record.deleted === 1 && r.gone, 'frames.cleanup',
      `one session, deleted; no picture unless asked for; ${leftOf(r)}`);
}
{
  const r = R.framediff;
  row(r.code === 1
      && /^DIFFERS {2}settings\.theme0: 1 of 10 lines differ; geometry \S+ text 0123456789abcdef; /m.test(r.out)
      && /^ {9}text: browser 0123456789abcdef, node 3e535f56470579c5$/m.test(r.out), 'frames.differ',
      `one line of settings differs: DIFFERS, and the line with both values (${said(r)})`);
  row(/^DIFFERS {2}chars\.colour\.theme0: 1 of 10 lines differ/m.test(r.out)
      && /^ {9}tag\.knob: not in the browser's frame, node 12$/m.test(r.out)
      && /^DIFFERS {2}modebrowser\.theme0: 1 of 11 lines differ/m.test(r.out)
      && /^ {9}tag\.extra: browser 3, not in the expectation$/m.test(r.out), 'frames.lines',
      'a line only one side has is a difference');
  row(count(r.out, /^EQUAL /mg) === 3 && /^PASS {5}live\/b\.html/m.test(r.out)
      && /^page-check: 6\/9 passed\npage-check: FAIL: chars\.colour\.theme0: 1 of 10 lines differ$/m.test(r.out),
      'frames.goes_on',
      'the other views and the live pages are still judged; the verdict names the first part that failed');
}
{
  const r = R.framebad;
  const hooks = { panel: 'idle 0', modebrowser: 'dpi 1', presetbrowser: 'clock free', settings: 'drawn 0' };
  const named = Object.keys(hooks).filter((v) => new RegExp(
    `^FAIL {5}${v}\\.theme0: the hooks line is '[^']*${hooks[v]}[^']*' `
    + '\\(want dpi 2 clock fixed \\.\\.\\. drawn 1 idle 1\\)', 'm').test(r.out));
  row(r.code === 1 && named.length === 4, 'frames.hooks',
      `not at 2x, on a free clock, not drawn or not idle: a FAIL though the lines are equal (${named.join(', ')})`);
  row(/^FAIL {5}chars\.sidechain\.theme0: Module\.fcmpFrame\(\) threw: Error: unreachable/m.test(r.out)
      && /^FAIL {5}chars\.colour\.theme0: Module\.fcmpFrame\(\) returned nothing/m.test(r.out)
      && /^page-check: 1\/7 passed$/m.test(r.out), 'frames.bad', 'a frame that throws and an empty one are FAILs');
}
{
  const r = R.frameless;
  row(r.code === 1 && r.ms < 20000
      && count(r.out, /^FAIL {5}\S+\.theme0: the module has no Module\.fcmpFrame \(/mg) === 6,
      'frames.nomodule', `a module without the export fails each view at once (${said(r)})`);
  const g = R.framegone;
  const refused = /^FAIL {5}\S+\.theme0: the editor did not come up \(the page says 'THIS BROWSER HAS NO WEBGL2\.'\)/mg;
  row(g.code === 1 && count(g.out, refused) === 6 && g.ms < 20000, 'frames.gone',
      `a page that refused fails each view at once, with what it says (${said(g)})`);
  const f = R.framefewer;
  row(f.code === 1 && /^FAIL {5}settings\.theme0: no expectation \(.*settings\.theme0\.node\.fp\)/m.test(f.out)
      && count(f.out, /^EQUAL /mg) === 5, 'frames.missing', 'a missing expectation is a FAIL, never a pass');
  const s = R.failframes;
  row(s.code === 1 && count(s.out, /^EQUAL /mg) === 6
      && /^page-check: 6\/7 passed\npage-check: FAIL: selftest: editor\.pixels$/m.test(s.out),
      'frames.after_fail', 'a failed self-test does not stop the capture pages');
}
// ---- the live pages -------------------------------------------------------------------------------------------------
{
  const r = R.livefail;
  row(r.code === 1 && /^PASS {5}live\/a\.html: the page says PASS/m.test(r.out)
      && /^FAIL {5}web\.live\.fake b\.html rows\.count/m.test(r.out)
      && /^FAIL {5}live\/b\.html: rows\.count \(/m.test(r.out)
      && /^page-check: FAIL: live\/b\.html: rows\.count$/m.test(r.out),
      'pages.fail', said(r));
  const h = R.livehang;
  row(h.code === 2 && h.err.includes(`no verdict within ${seconds(8)} s: live/b.html (the title was 'RUNNING')`)
      && /^PASS {5}live\/a\.html/m.test(h.out) && /^NOTE {5}the live page b\.html$/m.test(h.out)
      && h.record.deleted === 1 && h.gone,
      'pages.hang', `a live page with no verdict ends the run, named (${said(h)})`);
  const b = R.livebare;
  row(b.code === 0 && /^NOTE {5}.*live-bare has no page$/m.test(b.out) && /^page-check: 1\/1 passed$/m.test(b.out),
      'pages.none', 'a live directory with no page is a NOTE');
  row(R.all.record.urls.every((u) => !u.includes('notes.txt')), 'pages.html_only', 'only *.html is opened');
}
// ---- what each browser and each driver is asked for -----------------------------------------------------------------
{
  const r = R.capsChrome;
  const o = capsOf(r)['goog:chromeOptions'] || {};
  const a = o.args || [];
  const gl = process.platform === 'darwin' ? ['--use-angle=metal']
                                           : ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
  row(capsOf(r).browserName === 'chrome' && a.includes('--headless=new') && a.includes('--mute-audio')
      && gl.every((f) => a.includes(f)) && a.includes('--autoplay-policy=no-user-gesture-required')
      && a[a.length - 1] === '--use-angle=gl' && o.binary === '/opt/x/chrome'
      && Object.keys(capsOf(r)).sort().join() === 'browserName,goog:chromeOptions', 'caps.chrome',
      JSON.stringify(o));
  row(/^--port=\d+ --verbose$/.test(r.record.argv.join(' ')), 'caps.chrome.driver', r.record.argv.join(' '));
  const d = (capsOf(R.pass)['goog:chromeOptions'] || {}).args || [];
  row(d.includes('--mute-audio') && d.includes('--headless=new') && !d.some((f) => f.startsWith('--autoplay-policy')),
      'caps.chrome.default', 'headless and muted, and no autoplay unless asked');
  const h = (capsOf(R.capsHeaded)['goog:chromeOptions'] || {}).args || [];
  row(!h.includes('--headless=new') && h.includes('--mute-audio'), 'caps.chrome.headed');
}
{
  const r = R.capsFirefox;
  const o = capsOf(r)['moz:firefoxOptions'] || {};
  const p = o.prefs || {};
  row(r.code === 0 && capsOf(r).browserName === 'firefox' && (o.args || []).includes('-headless')
      && p['media.volume_scale'] === '0.0' && p['webgl.force-enabled'] === true && p['media.autoplay.default'] === 0
      && p['media.autoplay.blocking_policy'] === 0 && o.binary === '/opt/x/firefox', 'caps.firefox', JSON.stringify(o));
  row(/^--port \d+ --log debug$/.test(r.record.argv.join(' ')), 'caps.firefox.driver', r.record.argv.join(' '));
  const d = capsOf(R.capsFirefoxDefault)['moz:firefoxOptions'] || { prefs: {}, args: [] };
  row(d.prefs['media.volume_scale'] === '0.0' && !('media.autoplay.default' in d.prefs) && d.args.includes('-headless')
      && !('binary' in d), 'caps.firefox.default');
  // A snap Firefox (a Linux machine that has one): the profile goes where the snap can read it, unless --binary
  // names another Firefox. Anywhere else geckodriver is told nothing about it.
  const snap = process.platform === 'linux' && existsSync('/snap/firefox/current');
  const root = join(home, 'snap', 'firefox', 'common', 'page-check');
  const argv = R.capsFirefoxDefault.record.argv.join(' ');
  row(snap ? argv.endsWith(` --log debug --profile-root ${root}`) && existsSync(root)
           : /^--port \d+ --log debug$/.test(argv),
      'caps.firefox.snap', `${snap ? 'a snap Firefox' : 'no snap Firefox'}: ${argv}`);
}
{
  const refused = R.safariRefused;
  row(refused.code === 2 && refused.record === null && /only on a CI runner/.test(refused.err), 'caps.safari.refused',
      'outside GitHub Actions Safari is not driven: no driver was started');
  const r = R.safari;
  const c = capsOf(r);
  row(r.code === 0 && JSON.stringify(c) === '{"browserName":"safari"}', 'caps.safari', JSON.stringify(c));
  row(r.record !== null && /^--port \d+ --diagnose$/.test(r.record.argv.join(' '))
      && /---- .*com\.apple\.WebDriver\/fake\/safaridriver\.txt ----\nfake-driver: a diagnose file/.test(r.driverLog)
      && /--autoplay does nothing for Safari/.test(r.out), 'caps.safari.driver',
      'safaridriver is asked to diagnose, and what it wrote is in the driver\'s log');
  const here = R.safariHere;
  row(here.code === 0 && capsOf(here).browserName === 'safari', 'caps.safari.here',
      '--safari-here overrides the refusal');
}
{
  const c = R.driverEnvChrome, g = R.driverEnvGecko, p = R.driverPath;
  row(c.code === 0 && c.out.includes(`(${join(bin, 'chromedriver')} --port=`) && c.record !== null
      && g.code === 0 && g.out.includes(`(${join(bin, 'geckodriver')} --port `) && g.record !== null, 'driver.env',
      'with no --driver: $CHROMEWEBDRIVER/chromedriver and $GECKOWEBDRIVER/geckodriver');
  row(p.code === 0 && /^page-check: driver: fake-driver 1\.0 \(a test double\) \(geckodriver --port \d+ /.test(p.out)
      && p.record !== null, 'driver.path', 'with neither: the driver\'s name, found on PATH');
}
// ---- usage ----------------------------------------------------------------------------------------------------------
{
  const usage = ['usageNoBrowser', 'usageBrowser', 'usageOption', 'usageTwoSites', 'usageTimeout', 'usageValue',
                 'usageFrames', 'usagePages'];
  row(usage.every((k) => R[k].code === 2 && R[k].record === null && /^usage: page-check\.mjs <site> /m.test(R[k].err)),
      'usage', usage.map((k) => `${k.slice(5)} ${R[k].code}`).join(', '));
  row(/--frames needs --expect/.test(R.usageFrames.err) && /--pages needs --live/.test(R.usagePages.err)
      && /unknown option --page/.test(R.usageOption.err) && /--log needs a value/.test(R.usageValue.err),
      'usage.reason', 'the reason is said before the usage line');
  row(R.noIndex.code === 2 && R.noIndex.record === null && /nosite has no index\.html/.test(R.noIndex.err)
      && R.noLive.code === 2 && R.noLive.record === null && /no-such-directory is not a directory/.test(R.noLive.err),
      'usage.inputs', 'a site without index.html and a live directory that is not one are refused');
}
// ---- every run ------------------------------------------------------------------------------------------------------
{
  const started = Object.keys(R).filter((k) => R[k].record !== null);
  const leaks = started.filter((k) => !R[k].gone);
  row(leaks.length === 0, 'cleanup.every_run', leaks.length === 0
    ? `the driver and its browser gone after each of the ${started.length} runs that started one`
    : leaks.map((k) => `${k} (${leftOf(R[k])})`).join(', '));
  const refused = started.flatMap((k) => R[k].record.refused.map((why) => `${k}: ${why}`));
  const posts = started.reduce((n, k) => n + count(R[k].record.commands.join('\n'), /^POST /mg), 0);
  row(posts > 0 && refused.length === 0, 'protocol.requests', refused.length === 0
    ? `${posts} POSTs, each with a JSON object and Content-Type application/json, every Execute Script with its args`
    : `a real driver refuses ${refused.length}: ${refused.slice(0, 3).join('; ')}`);
}

rmSync(scratch, { recursive: true, force: true });
const counts = `${passed} row(s) passed, ${failed} failed (${runs} runs of the runner)`;
console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${counts}`);
process.exit(failed === 0 ? 0 : 1);
