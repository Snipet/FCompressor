#!/usr/bin/env node
// web/tests/support/fake-driver.mjs --port N | --port=N | --version: a WebDriver server for web/tests/pagecheck.mjs
// (ADR-93, the lead phase). It plays one scenario (FAKE_SCENARIO) and records what it was asked (FAKE_RECORD, a JSON
// file rewritten after every event). It registers no test: cmake/FcmpWeb.cmake scans web/tests/*.mjs only.
//
// No browser. A page is a small model (a title, a log, a window), and Execute Script runs the runner's own script
// against that model in a node:vm context, so the scripts are run and not only recognised: one that names anything
// but `window` and `document` is a javascript error here. On the first Navigate the fake asks the runner's server
// for a list of paths, so the record also says what that server serves; a capture page's frame is made from the
// expectation the server gives at /expect/.
//
// Scenarios. The driver: pass, noversion (--version fails), notready (/status says so for ever), silent (/status is
// never answered), nostatus (/status is an unknown command), nosession, noid (New Session answers with no
// sessionId), notimeouts (Set Timeouts is refused), slowquit (SIGTERM is ignored). The self-test: fail, running (no
// verdict), hang (a script is never answered), garbage (a reply that is not JSON), scripterror (every script fails),
// flaky (the first script on a page fails), crash (the connection is dropped and the driver exits a moment later),
// lost (the session is gone), noshot (no screenshot). The capture pages: framediff (settings differs in its text
// line, chars.colour lacks a line, modebrowser has one more), framebad (the hooks line says not idle for panel,
// dpi 1 for modebrowser, a free clock for presetbrowser and not drawn for settings; chars.sidechain throws,
// chars.colour returns nothing), frameless (no Module.fcmpFrame), framegone (the page refused). The live pages:
// livefail and livehang (b.html fails, or never gives a verdict).
import { mkdirSync, writeFileSync } from 'node:fs';
import { createServer, request as httpRequest } from 'node:http';
import { join } from 'node:path';
import { runInNewContext } from 'node:vm';

const scenario = process.env.FAKE_SCENARIO || 'pass';
if (process.argv.includes('--version')) {
  if (scenario === 'noversion') { console.error('fake-driver: unknown option --version'); process.exit(3); }
  console.log('fake-driver 1.0 (a test double)\nits second line');
  process.exit(0);
}
const recordPath = process.env.FAKE_RECORD || '';
let port = 0;
for (let i = 2; i < process.argv.length; i += 1) {
  if (process.argv[i] === '--port') port = Number(process.argv[i + 1]);
  else if (process.argv[i].startsWith('--port=')) port = Number(process.argv[i].slice(7));
}
const record = { pid: process.pid, argv: process.argv.slice(2), scenario, capabilities: null, timeouts: null, urls: [],
                 polls: 0, deleted: 0, screenshots: 0, lostContexts: 0, served: {}, commands: [] };
const save = () => { if (recordPath) writeFileSync(recordPath, JSON.stringify(record, null, 1)); };
save();
if (scenario === 'slowquit') process.on('SIGTERM', () => { record.sigterm = true; save(); });
setTimeout(() => process.exit(0), 150000).unref();      // a runner that failed to stop it leaves nothing for long
console.error(`fake-driver: scenario ${scenario} on port ${port}`);
// What safaridriver --diagnose would leave behind, under the home the test gives.
if (process.argv.includes('--diagnose') && process.env.FAKE_HOME) {
  const dir = join(process.env.FAKE_HOME, 'Library', 'Logs', 'com.apple.WebDriver', 'fake');
  mkdirSync(dir, { recursive: true });
  writeFileSync(join(dir, 'safaridriver.txt'), 'fake-driver: a diagnose file\n');
}

// One raw request to the runner's server (a raw path: fetch would normalise ../ away).
const raw = (base, method, path) => new Promise((done) => {
  const u = new URL(base);
  const r = httpRequest({ host: u.hostname, port: u.port, method, path }, (response) => {
    const chunks = [];
    response.on('data', (d) => chunks.push(d));
    response.on('end', () => {
      const body = Buffer.concat(chunks);
      done({ status: response.statusCode, type: response.headers['content-type'] || '', bytes: body.length,
             cache: response.headers['cache-control'] || '', text: body.length <= 4096 ? body.toString('utf8') : '' });
    });
  });
  r.on('error', (e) => done({ status: 0, type: String(e.code), bytes: 0, cache: '', text: '' }));
  r.end();
});

const PNG = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==';
const HOOKS = 'hooks dpi 2 clock fixed theme 0 dt 0.0166666675 settle 28 drawn 1 idle 1';

// ---- the pages ------------------------------------------------------------------------------------------------------
let page = null;                                        // { kind, name, polls, want }: what Navigate opened
// The frame a capture page gives: the expectation's lines under a hooks line, with another `live` count (a browser's
// differs, and it is not compared), changed as the scenario says.
function frameOf(view, want) {
  let hooks = HOOKS;
  let rows = want.trim().split('\n').map((l) => (l.startsWith('live ') ? 'live 99' : l));
  if (scenario === 'framediff' && view === 'settings')
    rows = rows.map((l) => (l.startsWith('text ') ? 'text 0123456789abcdef' : l));
  if (scenario === 'framediff' && view === 'chars.colour') rows = rows.filter((l) => !l.startsWith('tag.knob '));
  if (scenario === 'framediff' && view === 'modebrowser') rows.push('tag.extra 3');
  const wrong = { panel: ['idle 1', 'idle 0'], modebrowser: ['dpi 2', 'dpi 1'],
                  presetbrowser: ['clock fixed', 'clock free'], settings: ['drawn 1', 'drawn 0'] }[view];
  if (scenario === 'framebad' && wrong) hooks = HOOKS.replace(...wrong);
  if (scenario === 'framebad' && view === 'chars.colour') return '';
  return `${[hooks, ...rows].join('\n')}\n`;
}
// The page as a script sees it: `document` and `window`.
function world() {
  const p = page;
  const up = p.polls > 1;                               // the first read finds the page still loading
  let title = 'OTHER', log = '', status = '';
  const window = {};
  if (p.kind === 'selftest') {
    const verdict = scenario !== 'running' && p.polls > 3;
    title = !verdict ? 'RUNNING' : scenario === 'fail' ? 'FAIL: editor.pixels' : 'PASS';
    log = 'NOTE     a fake browser\n';
    if (verdict) log += 'PASS     web.selftest browser: everything\n';
    if (verdict && scenario === 'fail') log += 'FAIL     web.selftest editor.pixels: 7 of 255\n';
  } else if (p.kind === 'live') {
    const mine = p.name === 'b.html';
    const verdict = up && !(scenario === 'livehang' && mine);
    title = !verdict ? 'RUNNING' : scenario === 'livefail' && mine ? 'FAIL: rows.count' : 'PASS';
    log = `NOTE     the live page ${p.name}\n`;
    if (verdict) log += `${title === 'PASS' ? 'PASS' : 'FAIL'}     web.live.fake ${p.name} rows.count: 112\n`;
  } else if (p.kind === 'capture') {
    status = up ? 'A LOOP PLAYS THROUGH THE COMPRESSOR.' : 'LOADING';
    window.fcmpPage = { state: () => 'idle' };
    window.Module = { fcmpReady: () => {} };
    if (up && scenario === 'framegone') {
      status = 'THIS BROWSER HAS NO WEBGL2.';
      window.fcmpPage = { state: () => 'refused' };
    } else if (up) {
      window.Module.fcmpStatus = () => '{}';
      if (scenario !== 'frameless') {
        window.Module.fcmpFrame = () => {
          if (scenario === 'framebad' && p.name === 'chars.sidechain') throw new Error('unreachable');
          return frameOf(p.name, p.want);
        };
      }
    }
  }
  const gl = { RENDERER: 0x1f01,
               getParameter: (what) => (what === 0x9246 ? 'Fake Renderer (software)' : 'Fake'),
               getExtension: (name) => (name === 'WEBGL_debug_renderer_info' ? { UNMASKED_RENDERER_WEBGL: 0x9246 }
                 : name === 'WEBGL_lose_context' ? { loseContext: () => { record.lostContexts += 1; } } : null) };
  const elements = { 'funkgui-log': { textContent: log }, 'fcmp-status': { textContent: status } };
  const document = { title, getElementById: (id) => elements[id] || null,
                     createElement: () => ({ getContext: (kind) => (kind === 'webgl2' ? gl : null) }) };
  window.document = document;
  return { window, document };
}

// ---- the server -----------------------------------------------------------------------------------------------------
const SESSION = 'fake-session-1';
const server = createServer((request, response) => {
  let body = '';
  request.on('data', (d) => { body += d; });
  request.on('end', async () => {
    const send = (status, value) => {
      response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' });
      response.end(JSON.stringify({ value }));
    };
    const error = (status, code, message) => send(status, { error: code, message, stacktrace: '' });
    const path = request.url;
    record.commands.push(`${request.method} ${path.replace(/\/session\/[^/]+/, '/session/<id>')}`);
    save();
    if (request.method === 'GET' && path === '/status') {
      if (scenario === 'silent') return undefined;                      // never answered
      if (scenario === 'nostatus') return error(404, 'unknown command', 'GET /status');
      return send(200, { ready: scenario !== 'notready', message: '' });
    }
    if (request.method === 'POST' && path === '/session') {
      const asked = JSON.parse(body);
      record.capabilities = asked.capabilities && asked.capabilities.alwaysMatch;
      save();
      if (scenario === 'nosession')
        return error(500, 'session not created', 'session not created: the browser binary was not found\nat line 2');
      if (scenario === 'noid') return send(200, { capabilities: { browserName: 'chrome' } });
      return send(200, { sessionId: SESSION,
                         capabilities: { browserName: record.capabilities.browserName, browserVersion: '1.2.3',
                                         platformName: 'fake', 'fake:driverVersion': '1.0' } });
    }
    if (!path.startsWith(`/session/${SESSION}`)) return error(404, 'invalid session id', 'no such session');
    const rest = path.slice(`/session/${SESSION}`.length);
    if (request.method === 'POST' && rest === '/timeouts') {
      if (scenario === 'notimeouts') return error(400, 'invalid argument', 'timeouts are not for this driver');
      record.timeouts = JSON.parse(body);
      save();
      return send(200, null);
    }
    if (request.method === 'POST' && rest === '/url') {
      const url = new URL(JSON.parse(body).url);
      record.urls.push(url.href);
      const view = url.searchParams.get('view');
      const name = url.pathname.split('/').pop();
      page = { polls: 0, want: '', name: view || name,
               kind: url.pathname.startsWith('/live/') ? 'live' : view ? 'capture'
                   : url.searchParams.get('selftest') === '1' ? 'selftest' : 'other' };
      if (page.kind === 'capture') page.want = (await raw(url.origin, 'GET', `/expect/${view}.theme0.node.fp`)).text;
      if (record.urls.length === 1) {
        const ask = { page: ['GET', url.pathname + url.search], wasm: ['GET', '/fcmp-engine.wasm'],
                      script: ['GET', '/main.js'], head: ['HEAD', url.pathname], missing: ['GET', '/no-such-file.js'],
                      above: ['GET', '/../secret.txt'], aboveEncoded: ['GET', '/%2e%2e/secret.txt'],
                      aboveSlash: ['GET', '/licences/..%2f..%2fsecret.txt'], post: ['POST', url.pathname],
                      directory: ['GET', '/'], malformed: ['GET', '/%E0%A4%A'], nul: ['GET', '/a%00b'],
                      live: ['GET', '/live/a.html'], liveModule: ['GET', '/live/fcmp-print.wasm'],
                      liveUp: ['GET', '/live/../main.js'], liveAbove: ['GET', '/live/..%2fsecret.txt'],
                      liveSibling: ['GET', '/live/..%2fsite%2findex.html'],
                      expect: ['GET', '/expect/panel.theme0.node.fp'],
                      expectAbove: ['GET', '/expect/..%2fsecret.txt'] };
        for (const [key, [method, what]] of Object.entries(ask)) {
          const { text, ...answer } = await raw(url.origin, method, what);
          record.served[key] = { ...answer, secret: text.includes('not the site') };
        }
      }
      save();
      return send(200, null);
    }
    if (request.method === 'POST' && rest === '/execute/sync') {
      const { script, args } = JSON.parse(body);
      const reading = !/getContext/.test(script);                       // the renderer is asked after the verdict
      if (reading) {
        record.polls += 1;
        page.polls += 1;
        save();
        if (scenario === 'hang') return undefined;                      // never answered
        if (scenario === 'garbage') { response.writeHead(200); return response.end('<html>not json</html>'); }
        if (scenario === 'scripterror' || (scenario === 'flaky' && page.polls === 1))
          return error(500, 'javascript error', 'javascript error: document is not defined');
        if (scenario === 'crash' && record.polls === 3) {                 // the connection first, the process after it
          setTimeout(() => process.exit(7), 100);
          return request.socket.destroy();
        }
        if (scenario === 'lost' && record.polls === 3)
          return error(404, 'invalid session id', 'the browser has closed');
      }
      try {
        return send(200, runInNewContext(`(function () { ${script}\n}).apply(null, args)`, { ...world(), args },
                                         { timeout: 2000 }) ?? null);
      } catch (e) {
        return error(500, 'javascript error', `javascript error: ${e && e.message}`);
      }
    }
    if (request.method === 'GET' && rest === '/screenshot') {
      record.screenshots += 1;
      save();
      if (scenario === 'noshot') return error(500, 'unable to capture screen', 'no screen');
      return send(200, PNG);
    }
    if (request.method === 'DELETE' && rest === '') {
      record.deleted += 1;
      save();
      return send(200, null);
    }
    return error(404, 'unknown command', `${request.method} ${path}`);
  });
});
server.listen(port, '127.0.0.1');
