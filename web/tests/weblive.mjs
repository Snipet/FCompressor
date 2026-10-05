// web/tests/weblive.mjs: the browser gate's runner without a browser (ADR-93, the web lead phase).
//
// FCMP_WEB_TEST name=web.live.runner timeout=120 on=web args={source},{build}
//
//   node weblive.mjs <repository> <build directory>
//
// Scripts/web-live.sh needs Chrome, so it is never part of `verify`; what it stands on is held here. The pieces of
// Scripts/web/cdp.mjs and Scripts/web/live.mjs are imported as they are, and the scripts run as child processes
// where a row is about their arguments. No browser is started. The rows:
//   server.*     the gate's static server over scratch directories: the wasm type, no caching, HEAD, 404, 405, the
//                three roots (/ the site, /live/, /expect/), nothing above a root in any spelling (plain, encoded,
//                behind a prefix, a link that points out), what it was asked (with a request's word about the cache),
//                and a server that is gone once killed
//   compare.*    the browser's frame against the node value: equal, a differing line, a missing and an extra line,
//                `live` and `hooks` left out, no expectation (a FAIL, never a pass), and each way a hooks line is wrong
//   pixels.*     the pixel rule by renderer class, on both sides of each bound
//   page.*       a page's end by the self-test's protocol: no verdict within the timeout, a FAIL title, and a PASS
//                title that the log or an uncaught error contradicts; a log read on after an early FAIL to its last
//                line, until it is quiet, until the timeout, or until the page stops answering; and what a self-test
//                that passes must have judged (the sample loop's three rows, and the live audio with a running context)
//   scenario.*   the scenario's end by its contract: the last line, the exit code, a run that never ended; its
//                command line (four times --timeout, a forced renderer as --chrome-flag); a stand-in scenario with a
//                Chrome and a scratch directory stopped at the bound and by SIGINT, SIGTERM and SIGHUP to the gate:
//                its own cleanup runs, nothing of it is left
//   published.*  the published site's built-from.txt asked over HTTP (a local server playing the remote host, under
//                a sub path): another commit until a delay passes, a site that never updates, 404, no server; and its
//                audio/loop.wav: the repository's file passes, and one of another size, one with another byte, a
//                404 and no server each fail
//   contract.*   the capture page's address is the contract's, its pins are names Source/web/ui/WebMain.cpp reads,
//                the views are the editor's, and web-live.sh computes node values for the views the runner opens; the
//                sample loop's address, size and SHA-256 in the runner are the file's and the page's
//   summary.*    the count, the last line and the exit code of a run
//   chrome.*     how the library starts Chrome, seen by a stand-in executable that writes down its arguments and
//                ends: always --mute-audio and --headless=new, a throwaway profile that is gone afterwards, the GPU
//                flag by platform, the autoplay, sandbox and null-sink switches only where asked, no flag from any
//                other variable of the environment, a missing browser said; and, through a stand-in that speaks
//                DevTools over the pipe, the keys a typed text sends, the bound of page.until on a page that does
//                not answer, a tab's gate (one address answered in the server's place: each answer as the protocol
//                has it, a request that waits, one the page gave up) and the port tap's count of a reply's columns
//                (run under node on replies made here: the loudest and the quietest, the skip, a gap, and the first
//                columns kept one by one)
//   gate.*       web-live.sh with that stand-in: no verdict, exit 2, its first Chrome without the autoplay switch;
//                --gpu swiftshader; the results directory (a foreign one refused and untouched, in every form; an
//                earlier run's results replaced; the default beside the site, never the current directory); the
//                --url form with the stand-in server: `published`, then `audio`, and the pages after either verdict
//                of `audio`
//   script.*     what web-live.sh runs, seen by a stand-in for node: the results directory prepared first, the
//                twelve ui.dump calls of the contract with scratch preference paths, the expectation tool, the
//                runner's arguments for a build tree, an artifact, the published site and --serve, what a failing
//                ui.dump or tool leads to, and SIGINT, SIGTERM and SIGHUP during the node values (exit 2, no scratch
//                directory left)
//   usage.*      every way web-live.sh and live.mjs refuse to run, each with exit 2 and its reason: no arguments, an
//                unknown option, a missing value, both forms at once, no such directory, not a build tree, not a web
//                build, no built site, --dir without --live and --expect, a site without built-from.txt, no node,
//                a node older than 22, no Chrome (on the real build tree too), each misuse of --url, --commit, --wait
//                and --gpu; and --help
//   serve.*      web-live.sh --serve --dir from another directory, every path with a space in it: the URLs of the
//                contract with what each capture page must give, the three roots answering, and SIGINT, SIGTERM
//                and SIGHUP each ending it
// The test sets FCMP_WEB_LIVE_NO_SANDBOX itself for every run it makes (CI may have it set for the gate). Output:
// PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, realpathSync, rmSync,
         statSync, symlinkSync, writeFileSync } from 'node:fs';
import { createServer, request } from 'node:http';
import { tmpdir } from 'node:os';
import { basename, join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { runInNewContext } from 'node:vm';

const TEST = 'web.live.runner';
// The gate's switch reaches every Chrome the library starts (cdp.mjs reads it), and CI may have it set for the gate:
// this test sets it itself where a row is about it, and no other run of it sees it.
delete process.env.FCMP_WEB_LIVE_NO_SANDBOX;

let passed = 0;
let failed = 0;
function row(ok, name, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}     ${TEST} ${name}${detail ? ': ' + detail : ''}`);
  if (ok) passed += 1;
  else failed += 1;
  return ok;
}
function note(text) {
  console.log(`NOTE     ${text}`);
}

const [source, build] = process.argv.slice(2);
if (!source || !build) {
  console.error('usage: node weblive.mjs <repository> <build directory>');
  process.exit(2);
}
const script = join(source, 'Scripts', 'web-live.sh');
const runner = join(source, 'Scripts', 'web', 'live.mjs');
const cdp = await import(pathToFileURL(join(source, 'Scripts', 'web', 'cdp.mjs')).href);
const live = await import(pathToFileURL(runner).href);

// A space in its name: every path below has one. Its real path, as the script's `pwd -P` gives it.
const scratch = realpathSync(mkdtempSync(join(tmpdir(), 'fcmp weblive ')));
const removeScratch = () => rmSync(scratch, { recursive: true, force: true });
process.on('exit', removeScratch);                      // also when the library ends the process (an uncaught error)
const SECRET = 'above every root';
const WASM = Buffer.from([0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]);
const FP = (geometry, text) => `geometry ${geometry}\ntext ${text}\nstatics 864\nlive 3\ntexts 540\n`
                               + 'max_x 921.5\ntag.slot_label 131\nview_w 960\nview_h 640\nglyphs_missing 0\n';
const put = (path, content) => {
  mkdirSync(join(path, '..'), { recursive: true });
  writeFileSync(path, content);
};
const site = join(scratch, 'the site');
const liveDir = join(scratch, 'the live');
const expectDir = join(scratch, 'the expect');
put(join(scratch, 'secret.txt'), SECRET);
put(join(site, 'index.html'), '<!doctype html><title>site</title>');
put(join(site, 'main.js'), 'export {};\n');
put(join(site, 'fcmp-ui.wasm'), WASM);
put(join(site, 'built-from.txt'), `site ${'0'.repeat(40)} clean 2026-10-02T00:00:00Z\n`);
put(join(site, 'licences', 'GPL-3.0.txt'), 'licence\n');
put(join(site, 'audio', 'loop.wav'), 'RIFF....WAVE');
put(join(liveDir, 'page.html'), '<!doctype html><title>RUNNING</title>');
put(join(liveDir, 'golden', 'clean.dsp.print.txt'), 'rows\n');
live.VIEWS.forEach((view, i) => put(join(expectDir, `${view}.theme0.node.fp`), FP(`${i}`.repeat(16), 'f'.repeat(16))));
put(join(expectDir, 'rows.json'), '{}\n');
symlinkSync(join(scratch, 'secret.txt'), join(site, 'link.txt'));          // a link that points out of the root

// One request with the path sent as it is written (no client-side normalising).
const ask = (base, path, method = 'GET', headers = {}) => new Promise((answered) => {
  const url = new URL(base);
  const req = request({ host: url.hostname, port: url.port, path, method, headers, agent: false }, (res) => {
    const chunks = [];
    res.on('data', (c) => chunks.push(c));
    res.on('end', () => answered({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
  });
  req.on('error', (e) => answered({ status: 0, headers: {}, body: Buffer.alloc(0), error: e.code || e.message }));
  req.end();
});
const pause = (ms) => new Promise((r) => setTimeout(r, ms));
const readOr = (file) => (existsSync(file) ? readFileSync(file, 'utf8') : '');
// Whether a process still runs (a zombie waits for its parent, so give it a moment).
const ended = async (pid, ms = 3000) => {
  for (const t0 = Date.now(); Date.now() - t0 < ms; await pause(50)) {
    try { process.kill(pid, 0); } catch { return true; }
  }
  return false;
};

// A stand-in for Chrome that speaks DevTools over the pipe, as the library drives it (descriptor 3 in, 4 out, each
// message ended by a NUL), and draws nothing: every command is answered with {}, a version, a target and a session
// with theirs. Runtime.evaluate is answered `true`, or never ($FAKE_EVAL=never: a page whose main thread never rests).
// Input.dispatchKeyEvent's parameters go to $FAKE_KEYS, one JSON a line. Browser.close, or its driver gone, ends it.
// For the gate: every Fetch command goes to $FAKE_FETCH, one JSON a line; an evaluation of `request:<id>` is a page
// that asks for a file, which this browser stops (the event Fetch.requestPaused, to that tab's session); and an
// answer for a request whose id begins with `gone` is refused, as Chrome refuses one for a request the page gave up.
// Every expression evaluated goes to $FAKE_EVALS, one JSON a line. A page that is asked for the columns its tap kept
// answers $FAKE_LEVELS, a JSON text.
const devtools = join(scratch, 'devtools chrome.mjs');
writeFileSync(devtools, `import { appendFileSync } from 'node:fs';
import { Socket } from 'node:net';
const input = new Socket({ fd: 3, readable: true, writable: false });
const output = new Socket({ fd: 4, readable: false, writable: true });
const answer = (id, result) => output.write(JSON.stringify({ id, result }) + '\\0');
const handle = (m) => {
  if (m.method === 'Browser.getVersion') return answer(m.id, { product: 'DevToolsStandIn/1.0' });
  if (m.method === 'Target.createTarget') return answer(m.id, { targetId: 'T' + m.id });
  if (m.method === 'Target.attachToTarget') return answer(m.id, { sessionId: 'S' + m.id });
  if (m.method === 'Runtime.evaluate') {
    if (process.env.FAKE_EVALS) appendFileSync(process.env.FAKE_EVALS, JSON.stringify(m.params.expression) + '\\n');
    if (process.env.FAKE_LEVELS && /heard\\.levels\\)$/.test(m.params.expression)) {
      return answer(m.id, { result: { type: 'string', value: process.env.FAKE_LEVELS } });
    }
    const asks = /^request:(.+)$/.exec(m.params.expression);
    if (asks) {
      output.write(JSON.stringify({ method: 'Fetch.requestPaused', sessionId: m.sessionId, params: {
        requestId: asks[1], request: { url: 'http://site.invalid/audio/loop.wav', headers: { Accept: '*/*' } } } })
        + '\\0');
    }
    return process.env.FAKE_EVAL === 'never' ? undefined : answer(m.id, { result: { type: 'boolean', value: true } });
  }
  if (m.method.startsWith('Fetch.')) {
    if (process.env.FAKE_FETCH) {
      appendFileSync(process.env.FAKE_FETCH, JSON.stringify({ method: m.method, ...m.params }) + '\\n');
    }
    if (String(m.params.requestId).startsWith('gone')) {
      return output.write(JSON.stringify({ id: m.id, error: { message: 'Invalid InterceptionId.' } }) + '\\0');
    }
  }
  if (m.method === 'Input.dispatchKeyEvent' && process.env.FAKE_KEYS) {
    appendFileSync(process.env.FAKE_KEYS, JSON.stringify(m.params) + '\\n');
  }
  answer(m.id, {});
  if (m.method === 'Browser.close') setTimeout(() => process.exit(0), 20);
};
let held = Buffer.alloc(0);
input.on('data', (chunk) => {
  held = Buffer.concat([held, chunk]);
  for (let end = held.indexOf(0); end >= 0; end = held.indexOf(0)) {
    handle(JSON.parse(held.subarray(0, end).toString('utf8')));
    held = held.subarray(end + 1);
  }
});
input.on('end', () => process.exit(0));
input.on('error', () => process.exit(0));
`);
const devtoolsChrome = join(scratch, 'devtools chrome');
writeFileSync(devtoolsChrome, `#!/bin/sh\nexec '${process.execPath}' '${devtools}' "$@"\n`);
chmodSync(devtoolsChrome, 0o755);

try {
  // ---- the server ------------------------------------------------------------------------------------------------
  {
    const server = await cdp.serve(site, { live: liveDir, expect: expectDir });
    const get = (path, method) => ask(server.base, path, method);

    const wasm = await get('/fcmp-ui.wasm');
    row(wasm.status === 200 && wasm.headers['content-type'] === 'application/wasm' && wasm.body.equals(WASM),
        'server.wasm_type', `${wasm.status}, ${wasm.headers['content-type']}, ${wasm.body.length} bytes`);

    const types = [];
    for (const [path, type] of [['/index.html', 'text/html'], ['/main.js', 'text/javascript'],
                                ['/licences/GPL-3.0.txt', 'text/plain'], ['/expect/rows.json', 'application/json'],
                                ['/expect/panel.theme0.node.fp', 'text/plain'], ['/audio/loop.wav', 'audio/wav']]) {
      const r = await get(path);
      if (r.status !== 200 || !String(r.headers['content-type']).startsWith(type)) {
        types.push(`${path}: ${r.status} ${r.headers['content-type']}`);
      }
    }
    row(types.length === 0, 'server.types', types.join('; ') || 'html, js, txt, json, fp and wav have their types');

    // What it was asked, in order, with a request's own word about the cache: how a row tells a fetch that went
    // round the browser's cache from a plain one.
    const from = server.asked.length;
    await ask(server.base, '/audio/loop.wav');
    await ask(server.base, '/audio/loop.wav?again', 'GET', { 'Cache-Control': 'no-cache', Pragma: 'no-cache' });
    await ask(server.base, '/nope.wav', 'HEAD');
    const asked = server.asked.slice(from);
    row(from >= 7 && server.asked[0].url === '/fcmp-ui.wasm' && JSON.stringify(asked) === JSON.stringify([
      { method: 'GET', url: '/audio/loop.wav', cache: '' },
      { method: 'GET', url: '/audio/loop.wav?again', cache: 'no-cache' },
      { method: 'HEAD', url: '/nope.wav', cache: '' }]), 'server.asked',
        `every request is kept, the missed ones too: ${asked.map((r) => `${r.method} ${r.url} `
          + `(Cache-Control "${r.cache}")`).join(', ')}`);

    const answers = [await get('/'), wasm, await get('/nope.js'), await get('/../secret.txt'),
                     await get('/', 'POST')];
    row(answers.every((r) => r.headers['cache-control'] === 'no-store'), 'server.no_caching',
        answers.map((r) => `${r.status}: ${r.headers['cache-control']}`).join(', '));

    const head = await get('/fcmp-ui.wasm', 'HEAD');
    const headMiss = await get('/nope.js', 'HEAD');
    row(head.status === 200 && head.headers['content-length'] === String(WASM.length) && head.body.length === 0
        && head.headers['content-type'] === 'application/wasm' && headMiss.status === 404 && headMiss.body.length === 0,
        'server.head', `HEAD a file: ${head.status}, length ${head.headers['content-length']}, a body of `
        + `${head.body.length}; HEAD nothing: ${headMiss.status}`);

    const miss = [await get('/nope.js'), await get('/live/nope.html'), await get('/expect/nope.fp'),
                  await get('/licences')];
    row(miss.slice(0, 3).every((r) => r.status === 404) && server.misses.includes('404 GET /live/nope.html'),
        'server.not_found', `${miss.map((r) => r.status).join(', ')}; ${server.misses.length} requests kept as missed`);

    const post = await get('/index.html', 'POST');
    const del = await get('/index.html', 'DELETE');
    row(post.status === 405 && del.status === 405, 'server.methods', `POST ${post.status}, DELETE ${del.status}`);

    const want = [['/', 200, 'site'], ['/index.html', 200, 'site'], ['/live/page.html', 200, 'RUNNING'],
                  ['/live/golden/clean.dsp.print.txt', 200, 'rows'], ['/expect/panel.theme0.node.fp', 200, 'geometry'],
                  ['/live/main.js', 404, ''], ['/live/index.html', 404, ''], ['/expect/page.html', 404, ''],
                  ['/page.html', 404, ''], ['/panel.theme0.node.fp', 404, ''], ['/live/', 404, '']];
    const wrong = [];
    for (const [path, status, has] of want) {
      const r = await get(path);
      if (r.status !== status || !r.body.toString().includes(has)) wrong.push(`${path}: ${r.status}`);
    }
    row(wrong.length === 0, 'server.three_roots',
        wrong.join('; ')
        || '/ is the site, /live/ the live directory, /expect/ the expectations, and none leaks into another');

    const spellings = ['/../secret.txt', '/%2e%2e/secret.txt', '/..%2fsecret.txt', '/%2e%2e%2fsecret.txt',
                       '/%2E%2E/secret.txt', '/./../secret.txt', '/licences/../../secret.txt',
                       '/live/../secret.txt', '/live/../../secret.txt', '/live/%2e%2e/secret.txt',
                       '/live/..%2fsecret.txt', '/live/golden/../../secret.txt',
                       '/live/golden/%2e%2e/%2e%2e/secret.txt',
                       '/expect/../secret.txt', '/expect/%2e%2e/secret.txt', '/expect/..%2f..%2fsecret.txt',
                       '/..%5csecret.txt', '/%5c..%5csecret.txt', '/..\\secret.txt', '/link.txt', '/%2e%2e',
                       '/..', '/live/..', `/${encodeURIComponent(join(scratch, 'secret.txt'))}`,
                       '/secret.txt%00.html', '/%'];
    const leaks = [];
    for (const path of spellings) {
      const r = await get(path);
      if (r.status === 200 || r.status === 0 || r.body.toString().includes(SECRET)) leaks.push(`${path}: ${r.status}`);
    }
    row(leaks.length === 0, 'server.above',
        leaks.join('; ') || `${spellings.length} spellings of a path above a root, none answered with a file`);

    server.kill();
    const after = await get('/index.html');
    row(after.status === 0, 'server.killed', `after kill() a request gives ${after.error || after.status}`);

    const plain = await cdp.serve(site);
    const noLive = await ask(plain.base, '/live/page.html');
    const index = await ask(plain.base, '/index.html');
    row(/^http:\/\/127\.0\.0\.1:\d+$/.test(plain.base) && plain.port > 0 && plain.port !== server.port
        && index.status === 200 && noLive.status === 404, 'server.one_root',
        `${plain.base}: with no live directory /live/page.html is ${noLive.status}`);
    plain.kill();
  }

  // ---- the comparison ----------------------------------------------------------------------------------------------
  {
    const HOOKS = `hooks dpi 2 clock fixed theme 0 dt ${live.DT} settle 29 drawn 1 idle 1\n`;
    const node = FP('738cee5019a4f33c', '3e535f56470579c4');
    const want = { theme: 0, clock: 'fixed' };
    const judged = (liveText, expectText = node, w = want) => live.judgeFrame(liveText, expectText, w);
    const list = (v) => `${v.status}${v.why ? ` (${v.why})` : ''}${v.differ.length ? ` [${v.differ.join('; ')}]` : ''}`;

    const equal = judged(HOOKS + node);
    row(equal.status === 'EQUAL' && equal.compared === 9 && equal.differ.length === 0
        && equal.live.geometry === '738cee5019a4f33c' && equal.node.text === '3e535f56470579c4', 'compare.equal',
        `${list(equal)}, ${equal.compared} lines compared`);

    const changed = ['geometry 738cee5019a4f33d', 'text 0000000000000000', 'statics 865', 'max_x 921.25',
                     'tag.slot_label 130', 'view_w 961', 'glyphs_missing 1'].map((line) => {
      const name = line.split(' ')[0];
      const v = judged(HOOKS + node.replace(new RegExp(`^${name.replace('.', '\\.')} .*$`, 'm'), line));
      const named = v.status === 'DIFFERS' && v.differ.length === 1 && v.differ[0].startsWith(`${name}: live `);
      return named ? '' : `${line}: ${list(v)}`;
    }).filter((bad) => bad !== '');
    row(changed.length === 0, 'compare.a_line_differs',
        changed.join('; ') || 'each of seven lines changed alone gives DIFFERS and names that line');

    const less = judged(HOOKS + node.replace('tag.slot_label 131\n', ''));
    const more = judged(HOOKS + node + 'tag.settings_text 406\n');
    row(less.status === 'DIFFERS' && /^tag\.slot_label: not in the browser's frame/.test(less.differ[0])
        && more.status === 'DIFFERS' && /^tag\.settings_text: live 406, not in the node value/.test(more.differ[0]),
        'compare.missing_and_extra', `a line less: ${list(less)}; a line more: ${list(more)}`);

    const liveOnly = judged(HOOKS.replace('settle 29', 'settle 1') + node.replace('live 3', 'live 51'));
    row(liveOnly.status === 'EQUAL' && liveOnly.compared === 9, 'compare.live_and_hooks_left_out',
        `another live count and another settle count: ${list(liveOnly)}`);

    const none = [null, undefined, '', 'statics 864\n', 'geometry 738cee5019a4f33c\n']
      .map((expectation) => live.judgeFrame(HOOKS + node, expectation, want));
    row(none.every((v) => v.status === 'FAIL' && v.why !== '') && none[0].why === 'no expectation',
        'compare.no_expectation', none.map(list).join('; '));

    const empty = [judged(''), judged(null)];
    row(empty.every((v) => v.status === 'FAIL' && /gave nothing/.test(v.why) && v.differ.length === 0),
        'compare.no_frame', empty.map(list).join('; '));

    const hooks = [['dpi 1', 'dpi 2', 'dpi 1'], ['clock free', 'clock fixed', 'clock free'],
                   ['theme 1', 'theme 0', 'theme 1'], ['dt 0.0333333351', `dt ${live.DT}`, 'dt 0.0333333351'],
                   ['drawn 0', 'drawn 1', 'drawn 0'], ['idle 0', 'idle 1', 'idle 0'],
                   ['settle x', 'settle 29', 'settle x']].map(([what, from, to]) => {
      const v = judged(HOOKS.replace(from, to) + node);
      return v.status === 'FAIL' && v.why !== '' ? '' : `${what}: ${list(v)}`;
    }).filter((bad) => bad !== '');
    const noHooks = judged(node);
    const freeOk = judged(HOOKS.replace('clock fixed', 'clock free').replace(live.DT, '0.001') + node, node,
                          { theme: 0, clock: 'free' });
    const freeBad = judged(HOOKS + node, node, { theme: 0, clock: 'free' });
    const both = judged(HOOKS.replace('dpi 2', 'dpi 1') + node.replace('statics 864', 'statics 1'));
    row(hooks.length === 0 && noHooks.status === 'FAIL' && freeOk.status === 'EQUAL' && freeBad.status === 'FAIL'
        && both.status === 'FAIL' && both.differ.length === 1, 'compare.hooks',
        hooks.join('; ') || `a wrong dpi, clock, theme, dt, drawn, idle or form is a FAIL, and so is no hooks line `
        + `(${noHooks.why}); a free clock passes only where it is wanted`);
  }

  // ---- the pixels ---------------------------------------------------------------------------------------------------
  {
    const METAL = 'ANGLE (Apple, ANGLE Metal Renderer: Apple M5, Unspecified Version)';
    const SWIFT = 'ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (Subzero) (0x0000C0DE)), SwiftShader driver)';
    const px = (largest, over2, samples = 1000000, frames = 1) => ({ frames, largest, over2, samples });
    const classes = [[METAL, 'gpu'], [SWIFT, 'software'], ['llvmpipe (LLVM 15.0.7, 256 bits)', 'software'],
                     ['softpipe', 'software'], ['Microsoft Basic Render Driver', 'software'],
                     ['Apple Software Renderer', 'software'], ['', 'gpu'], ['a renderer nobody has heard of', 'gpu']];
    const wrongClass = classes.filter(([name, cls]) => live.rendererClass(name) !== cls).map(([name]) => name);
    row(wrongClass.length === 0, 'pixels.class', wrongClass.join('; ')
        || 'SwiftShader, llvmpipe, softpipe and the software rasterisers are software; anything else is a GPU');
    const cases = [[px(2, 0), METAL, true], [px(3, 1), METAL, false], [px(1, 0, 0), METAL, false],
                   [px(1, 0, 1000000, 0), METAL, false], [undefined, METAL, false],
                   [px(16, 10000), SWIFT, true], [px(17, 1), SWIFT, false], [px(16, 10001), SWIFT, false],
                   [px(7, 650), SWIFT, true], [px(7, 650), METAL, false], [px(2, 0, 0), SWIFT, false]];
    const wrong = cases.filter(([p, name, ok]) => live.pixelRule(p, name).ok !== ok)
      .map(([p, name]) => `${JSON.stringify(p)} on ${live.rendererClass(name)}`);
    row(wrong.length === 0, 'pixels.rule', wrong.join('; ')
        || 'a GPU: none over 2; software: none over 16 and at most 10 per mille over 2; no frame or no sample fails');
  }

  // ---- a page's verdict ---------------------------------------------------------------------------------------------
  {
    const LOG = 'NOTE     a note\nPASS     web.selftest browser: ok\nPASS     web.selftest page.start: ok\n';
    const judge = (title, log = LOG, uncaught = []) => live.judgePage({ title, log, uncaught, timeoutS: 5 });
    const pass = judge('PASS');
    row(pass.ok && /2 row/.test(pass.detail), 'page.pass', pass.detail);
    const never = [judge('RUNNING'), judge(''), judge('FCompressor: web demo')];
    const threw = judge('RUNNING', LOG, ['EXCEPTION: Uncaught Error: boom']);
    row(never.every((v) => !v.ok && /no verdict within 5 s/.test(v.detail)) && /'RUNNING'/.test(never[0].detail)
        && !threw.ok && /no verdict after an uncaught error: EXCEPTION: Uncaught Error: boom/.test(threw.detail),
        'page.no_verdict', `${never[0].detail}; ${threw.detail}`);
    const fail = judge('FAIL: page.start', LOG.replace('PASS     web.selftest page.start',
                                                       'FAIL     web.selftest page.start'));
    row(!fail.ok && fail.detail.startsWith('FAIL: page.start') && /1 row\(s\) passed, 1 failed/.test(fail.detail),
        'page.fail', fail.detail);
    const lying = [judge('PASS', LOG + 'FAIL     web.selftest editor.pixels: 3 over 2\n'),
                   judge('PASS', LOG, ['EXCEPTION: Uncaught ReferenceError: x is not defined']),
                   judge('PASS', 'NOTE     nothing ran\n'), judge('PASS', ''),
                   judge('PASS', 'PASS     web.live.print: 0 row(s) passed, 0 failed\n')];
    row(lying.every((v) => !v.ok), 'page.pass_contradicted', lying.map((v) => v.detail).join('; '));
    row(live.isVerdict('PASS') && live.isVerdict('FAIL') && live.isVerdict('FAIL: x') && !live.isVerdict('RUNNING')
        && !live.isVerdict('PASSED') && !live.isVerdict(''), 'page.is_verdict',
        'PASS and FAIL... are verdicts; RUNNING, PASSED and an empty title are not');

    // An uncaught error makes the title FAIL at once, and the page goes on logging rows: the log is read on. Each
    // case is a page as a list of reads ([title, log]; undefined: no answer), one every 20 ms, the last one repeated.
    const UNCAUGHT = 'PASS     web.live.x one: ok\nFAIL     web.live.x uncaught: rejection: boom\n';
    const END = 'FAIL     web.live.x: 3 row(s) passed, 1 failed\n';
    const page = (reads) => {
      let i = 0;
      return async () => reads[Math.min(i++, reads.length - 1)];
    };
    const grows = (n) => Array.from({ length: n }, (_, k) => ['FAIL: uncaught',
      UNCAUGHT + Array.from({ length: k + 1 }, (__, j) => `PASS     web.live.x row${j}: ok\n`).join('')]);
    const forever = () => {                             // a new line at every read
      let k = 0;
      return async () => ['FAIL: uncaught', `${UNCAUGHT}PASS     web.live.x row: ${k++}\n`];
    };
    const timed = async (read, options) => {
      const t0 = Date.now();
      const r = await live.readToEnd(read, ['FAIL: uncaught', UNCAUGHT],
                                     { leftMs: 3000, quietMs: 600, every: 20, ...options });
      return { ...r, ms: Date.now() - t0 };
    };
    const toEnd = await timed(page([...grows(5), ['FAIL: uncaught', grows(5)[4][1] + END]]));
    const quiet = await timed(page(grows(3)));
    const busy = await timed(forever(), { leftMs: 800, quietMs: 600 });
    const deaf = await timed(page([undefined]));
    const already = await live.readToEnd(page([]), ['FAIL: uncaught', UNCAUGHT + END], { leftMs: 3000, quietMs: 600 });
    row(toEnd.how === 'last line' && toEnd.log.endsWith(END) && /row4: ok/.test(toEnd.log) && toEnd.ms < 600
        && quiet.how === 'quiet' && /row2: ok\n$/.test(quiet.log) && quiet.ms >= 600 && quiet.ms < 1500
        && busy.how === 'timeout' && busy.ms >= 800 && busy.ms < 1500
        && deaf.how === 'no answer' && deaf.log === UNCAUGHT && already.how === 'last line',
        'page.log_to_its_end', `after a FAIL title the log is read on: to its last line (${toEnd.ms} ms, `
        + `${toEnd.log.split('\n').length - 1} lines), until it is quiet (${quiet.ms} ms), until the timeout `
        + `(${busy.ms} ms), until the page stops answering; a log that has its last line is not read again`);

    // What a self-test that passes must have judged: the sample loop's three rows, and page.audio where its context
    // ran. A row that failed, or that is only named in another row's detail, is not a row that was judged.
    const rowsOf = (names, word = 'PASS') => names.map((name) => `${word}     web.selftest ${name}: detail\n`).join('');
    const SUSPENDED = rowsOf(['browser', 'engine.silence', ...live.SAMPLE_ROWS, 'page.start', 'editor.link']);
    const without = (name) => SUSPENDED.replace(rowsOf([name]), '');
    const gaps = live.SAMPLE_ROWS.map((name) => live.unjudged(without(name)));
    const failedRow = live.unjudged(without('sample.fit') + rowsOf(['sample.fit'], 'FAIL'));
    const named = live.unjudged(without('page.source') + 'NOTE     the row page.source: was not run\n');
    const noAudio = live.unjudged(SUSPENDED, { audio: true });
    row(live.SAMPLE_ROWS.join() === 'sample.read,sample.fit,page.source' && live.unjudged(SUSPENDED) === ''
        && live.unjudged(SUSPENDED + rowsOf(['page.audio']), { audio: true }) === ''
        && gaps.every((said, i) => said.startsWith(`the page did not judge ${live.SAMPLE_ROWS[i]}:`))
        && /did not judge sample\.fit:/.test(failedRow) && /did not judge page\.source:/.test(named)
        && /did not judge page\.audio: under the autoplay flag/.test(noAudio)
        && /did not judge sample\.read, sample\.fit, page\.source:/.test(live.unjudged('', { audio: true })),
        'page.selftest_rows', `a log with ${live.SAMPLE_ROWS.join(', ')} passes, and with page.audio where the `
        + `context ran; without one of them: "${gaps[0]}"; a row that failed or is only named does not count; `
        + `without page.audio under the autoplay flag: "${noAudio}"`);
  }

  // ---- the scenario's end -------------------------------------------------------------------------------------------
  {
    const OUT = 'PASS  start: running\nNOTE  time: 40 s\nscenario: 25/25 passed\n';
    const judge = (code, output, timedOutS = 0) => live.judgeScenario({ code, output, timedOutS });
    const good = judge(0, OUT);
    const bad = [judge(1, OUT.replace('25/25', '24/25')), judge(0, OUT.replace('25/25', '24/25')), judge(1, OUT),
                 judge(2, OUT), judge(0, OUT.replace('25/25', '0/0')), judge(0, 'PASS  start: running\n'),
                 judge(1, "Error [ERR_MODULE_NOT_FOUND]: Cannot find module 'lib.mjs'\n"), judge(0, ''),
                 judge(0, OUT + 'DRIVER ERROR late\n'), judge(null, OUT, 600), judge('SIGKILL', OUT),
                 judge(1, "Error [ERR_MODULE_NOT_FOUND]: Cannot find module 'lib.mjs'\n    at resolve\nNode.js v24\n")];
    const passing = bad.filter((v) => v.ok).map((v) => v.detail);
    row(good.ok && passing.length === 0 && /did not run by the contract/.test(bad[6].detail)
        && /no end within 600 s/.test(bad[9].detail)
        && /'Node\.js v24' \(it said: Error \[ERR_MODULE_NOT_FOUND\]: Cannot find module 'lib\.mjs'\)/
          .test(bad[11].detail), 'scenario.end',
        passing.length ? `passed, and should not: ${passing.join('; ')}`
          : `${good.detail}; and ${bad.length} ends that fail: a row failed, the count and the exit code disagree, no `
            + 'last line, nothing judged, killed, never ended');

    // Its command line: its own bound is four times the gate's --timeout (the gate stops it at five times), and a
    // renderer forced with --gpu reaches its Chrome as --chrome-flag switches.
    const opt = { scenario: '/s/scenario.mjs', dir: '/the site', out: '/the out', timeoutS: 7, gpu: undefined };
    const plain = live.scenarioArgs(opt, '/c');
    const forced = live.scenarioArgs({ ...opt, gpu: 'swiftshader' }, '/c');
    const byDefault = live.scenarioArgs({ ...opt, timeoutS: live.parseArgs(['--dir', 'a', '--live', 'b', '--expect',
                                                                            'c', '--out', 'd']).opt.timeoutS }, '/c');
    const wantPlain = ['/s/scenario.mjs', '--dir', '/the site', '--out', '/the out/scenario', '--chrome', '/c',
                       '--timeout', '28'];
    const gpuOf = (value) => live.parseArgs(['--dir', 'a', '--live', 'b', '--expect', 'c', '--out', 'd', '--gpu',
                                             value]);
    row(JSON.stringify(plain) === JSON.stringify(wantPlain)
        && JSON.stringify(forced) === JSON.stringify([...wantPlain, '--chrome-flag', '--use-angle=swiftshader',
                                                      '--chrome-flag', '--enable-unsafe-swiftshader'])
        && byDefault.join(' ').endsWith('--timeout 480') && gpuOf('swiftshader').opt.gpu === 'swiftshader'
        && gpuOf('default').opt.gpu === undefined && /--gpu is default or swiftshader/.test(gpuOf('metal').error),
        'scenario.arguments', `${plain.slice(1).join(' ')}; with --gpu swiftshader also `
        + `${forced.slice(plain.length).join(' ')}; 480 s at the default --timeout`);

    // A stand-in scenario as Scripts/web/scenario/driver.mjs has it: a scratch directory under its --out, removed by
    // its exit handler, and in it the profile of its Chrome (the DevTools stand-in), started through the library.
    // It never ends. The gate's part of it runs in a child process (live.runScenario, with the library's signal
    // handlers), so it can be ended by its bound or by a signal.
    const stub = join(scratch, 'stand-in scenario.mjs');
    writeFileSync(stub, `import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { basename, join } from 'node:path';
import { pathToFileURL } from 'node:url';
const args = process.argv.slice(2);
const at = (name) => args[args.indexOf(name) + 1];
const cdp = await import(pathToFileURL(process.env.STUB_CDP).href);
mkdirSync(at('--out'), { recursive: true });
const scratch = mkdtempSync(join(at('--out'), 'scenario-'));
process.on('exit', () => {
  rmSync(scratch, { recursive: true, force: true });
  writeFileSync(join(at('--out'), '..', 'its cleanup ran'), '');
});
mkdirSync(join(scratch, 'profile'));
const browser = await cdp.chrome({ chrome: at('--chrome'), profile: join(scratch, 'profile') });
console.log('READY ' + process.pid + ' ' + browser.pid + ' ' + scratch);
await new Promise(() => {});
`);
    const part = join(scratch, 'the gate part.mjs');
    writeFileSync(part, `import { pathToFileURL } from 'node:url';
const [runner, out, stub, exe, timeoutS] = process.argv.slice(2);
const live = await import(pathToFileURL(runner).href);
const end = await live.runScenario({ scenario: stub, dir: '/no site', out, timeoutS: Number(timeoutS) }, exe,
                                   new live.Tally());
console.log('END late ' + end.late + ' code ' + end.code);
process.exit(0);
`);
    // One run: the gate's part started, ended by `signal` once the stand-in is ready (or by its bound), and then
    // what is left: the scenario's process, its Chrome, its scratch directory.
    const scenarioRun = async (signal, timeoutS) => {
      const out = join(scratch, `scenario run ${signal}`);
      const child = spawn(process.execPath, [part, runner, out, stub, devtoolsChrome, String(timeoutS)],
                          { stdio: ['ignore', 'pipe', 'pipe'], env: { ...process.env, STUB_CDP: join(source, 'Scripts',
                                                                                   'web', 'cdp.mjs') } });
      let said = '';
      const t0 = Date.now();
      const closed = new Promise((r) => child.on('close', (code, sig) => r(code ?? sig)));
      const ready = await new Promise((r) => {
        const hear = (c) => { said += c; const m = /READY (\d+) (\d+) (.+)/.exec(said); if (m) r(m); };
        child.stdout.on('data', hear);
        child.stderr.on('data', (c) => { said += c; });
        closed.then(() => r(null));
        setTimeout(() => r(null), 20000);
      });
      if (ready && signal !== 'bound') child.kill(signal);
      const code = await Promise.race([closed, pause(20000).then(() => 'still running')]);
      if (code === 'still running') child.kill('SIGKILL');
      const pids = ready ? [Number(ready[1]), Number(ready[2])] : [];
      const gone = (await Promise.all(pids.map((pid) => ended(pid)))).every((is) => is);
      for (const pid of pids) { try { process.kill(pid, 'SIGKILL'); } catch { /* gone */ } }
      const left = ready ? existsSync(ready[3].trim()) : true;
      const leftovers = existsSync(join(out, 'scenario')) ? readdirSync(join(out, 'scenario')) : [];
      const cleaned = existsSync(join(out, 'its cleanup ran'));
      return { ok: ready !== null && gone && !left && leftovers.length === 0 && cleaned, code, said,
               ms: Date.now() - t0,
               text: `${signal}: ${ready ? 'ready' : `never ready (${said.trim().slice(-200)})`}, exit ${code}, `
                     + `the scenario and its Chrome ${gone ? 'gone' : 'STILL RUNNING'}, its own cleanup `
                     + `${cleaned ? 'ran' : 'DID NOT RUN'}, its scratch `
                     + `${left || leftovers.length ? `LEFT (${leftovers.join(', ')})` : 'gone'}` };
    };
    const bound = await scenarioRun('bound', 0.6);
    row(bound.ok && bound.code === 0 && /END late true code null/.test(bound.said) && bound.ms < 15000,
        'scenario.bound', `at the bound of 3 s: ${bound.text} (${bound.ms} ms)`);
    const signals = [];
    for (const signal of ['SIGINT', 'SIGTERM', 'SIGHUP']) signals.push(await scenarioRun(signal, 60));
    row(signals.every((r) => r.ok && r.code === 2 && r.ms < 15000), 'scenario.interrupted',
        signals.map((r) => r.text).join('; '));
  }

  // ---- the published site -------------------------------------------------------------------------------------------
  {
    // A server playing the remote host: the site under a sub path, its built-from.txt naming another commit until
    // `switchAt` (never, when it is Infinity).
    const OLD = `site ${'1'.repeat(40)} clean 2026-10-01T00:00:00Z`;
    const NEW = `site abcdef1${'2'.repeat(33)} clean 2026-10-02T00:00:00Z`;
    let switchAt = Infinity;
    const remote = createServer((req, res) => {
      if (req.url !== '/some/sub/path/built-from.txt') {
        res.writeHead(404);
        res.end();
        return;
      }
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end(`${Date.now() >= switchAt ? NEW : OLD}\n`);
    });
    await new Promise((r) => remote.listen(0, '127.0.0.1', r));
    const base = `http://127.0.0.1:${remote.address().port}/some/sub/path`;
    const waited = async (options) => {
      const notes = [];
      const t0 = Date.now();
      const r = await live.awaitPublished({ base, waitS: 4, say: (text) => notes.push(text), ...options });
      return { ...r, notes, ms: Date.now() - t0 };
    };
    switchAt = Date.now() + 1500;
    const later = await waited({ commit: 'abcdef1' });
    switchAt = Infinity;
    const never = await waited({ commit: 'abcdef1' });
    const now = await waited({ commit: '1'.repeat(40) });
    const any = await waited({});
    const missing = await waited({ base: `${base}/nothing`, commit: 'abcdef1', waitS: 1 });
    remote.close();
    const closed = await waited({ commit: 'abcdef1', waitS: 1 });
    row(later.ok && later.notes.length >= 1 && /says 'site 1{40} clean .*', not yet 'site abcdef1 clean'/
          .test(later.notes[0]) && later.ms >= 1500 && later.ms < 4500
        && !never.ok && /still says 'site 1{40} clean .*' after .* not 'site abcdef1 clean': the site was not updated/
          .test(never.detail) && never.ms >= 4000 && never.ms < 8000 && never.notes.length >= 2
        && now.ok && now.notes.length === 0 && any.ok && any.notes.length === 0
        && !missing.ok && /HTTP 404/.test(missing.detail) && !closed.ok && /no answer/.test(closed.detail),
        'published.built_from', `another commit until 1.5 s: ${later.detail}, ${later.notes.length} wait(s); never `
        + `updated: ${never.detail.replace(base, '<base>')} (${never.ms} ms, ${never.notes.length} NOTE(s)); 404 `
        + `and no server fail`);
  }
  {
    // The same host with the sample loop under its sub path: the repository's file, and four ways it is not.
    const loop = readFileSync(join(source, 'web', 'audio', 'loop.wav'));
    const flipped = Buffer.from(loop);
    flipped[flipped.length >> 1] ^= 1;                                // the same size, one bit of one sample
    const files = { '/right/audio/loop.wav': loop, '/flipped/audio/loop.wav': flipped,
                    '/cut/audio/loop.wav': loop.subarray(0, loop.length >> 1),
                    '/longer/audio/loop.wav': Buffer.concat([loop, Buffer.alloc(1)]) };
    const remote = createServer((req, res) => {
      const bytes = files[req.url];
      res.writeHead(bytes ? 200 : 404, bytes ? { 'Content-Type': 'audio/wav', 'Content-Length': bytes.length } : {});
      res.end(bytes || '');
    });
    await new Promise((r) => remote.listen(0, '127.0.0.1', r));
    const host = `http://127.0.0.1:${remote.address().port}`;
    const judged = {};
    for (const name of ['right', 'flipped', 'cut', 'longer', 'missing']) {
      judged[name] = await live.judgeAudio({ base: `${host}/${name}`, timeoutS: 20 });
    }
    remote.close();
    remote.closeAllConnections();
    judged.closed = await live.judgeAudio({ base: `${host}/right`, timeoutS: 5 });
    const sha = (bytes) => createHash('sha256').update(bytes).digest('hex');
    row(judged.right.ok && judged.right.detail.includes(`${host}/right/audio/loop.wav: ${loop.length} bytes`)
        && !judged.flipped.ok && judged.flipped.detail.includes(`${loop.length} bytes, SHA-256 ${sha(flipped)}; the `
                                                                + `sample loop's is ${live.AUDIO.sha256}`)
        && !judged.cut.ok && judged.cut.detail.includes(`${loop.length >> 1} bytes`)
        && judged.cut.detail.endsWith(`the sample loop has ${live.AUDIO.bytes} bytes`)
        && !judged.longer.ok && judged.longer.detail.includes(`${loop.length + 1} bytes`)
        && !judged.missing.ok && /HTTP 404$/.test(judged.missing.detail)
        && !judged.closed.ok && /no answer/.test(judged.closed.detail), 'published.audio',
        `the repository's file: ${judged.right.detail.replace(host, '<base>')}. One bit of it changed: `
        + `${judged.flipped.detail.split(': ').slice(1).join(': ').slice(0, 44)}...; half of it: `
        + `${judged.cut.detail.split(': ').slice(1).join(': ').replace(/, SHA-256 [0-9a-f]+/, '')}; one byte more, a `
        + '404 and no server fail too');
  }

  // ---- the contract -------------------------------------------------------------------------------------------------
  {
    const url = live.captureUrl('http://127.0.0.1:1', 'chars.sidechain', 1);
    const wantUrl = 'http://127.0.0.1:1/index.html?view=chars.sidechain&theme=1&zoom=100&scale=2&dt=0.0166666675'
                  + '&nohint=1&nolive=1&host=WEB-LIVE';
    const free = live.captureUrl('http://127.0.0.1:1', 'chars.sidechain', 0, '');
    row(url === wantUrl && !free.includes('dt=') && free.includes('&nolive=1&host=WEB-LIVE'), 'contract.capture_url',
        url);

    // The pins the runner sends are names the module reads. The lead-phase base's module does not read `host` yet
    // (its prototype switch PROTO_FP is still there): until that is gone, that one pin is a NOTE.
    const module = readFileSync(join(source, 'Source/web/ui/WebMain.cpp'), 'utf8');
    const pins = [...new URL(wantUrl).searchParams.keys()];
    let unread = pins.filter((pin) => !module.includes(`"${pin}"`));
    if (unread.join() === 'host' && /^#define PROTO_FP\b/m.test(module)) {
      note('WebMain.cpp is the lead-phase prototype: it does not read the host pin yet');
      unread = [];
    }
    row(pins.length === 8 && unread.length === 0, 'contract.pins_read_by_the_module',
        unread.length ? `WebMain.cpp does not name ${unread.join(', ')}` : pins.join(', '));

    const panel = readFileSync(join(source, 'Source/editor/Panel.cpp'), 'utf8');
    const strangers = live.VIEWS.filter((view) => !panel.includes(`{ "${view}",`));
    row(live.VIEWS.length === 6 && strangers.length === 0 && live.VIEWS.includes(live.ASYNC_VIEW),
        'contract.views_are_the_editors', strangers.length ? `not in Panel.cpp's views(): ${strangers.join(', ')}`
        : live.VIEWS.join(', '));

    const sh = readFileSync(script, 'utf8');
    const shValue = (name) => (new RegExp(`^${name}="?([^"\\n]*)"?$`, 'm').exec(sh) || [])[1];
    const guiLive = readFileSync(join(source, 'Scripts/gui-live.sh'), 'utf8');
    row(shValue('VIEWS') === live.VIEWS.join(' ') && shValue('THEMES') === live.THEMES.join(' ')
        && shValue('HOST') === live.HOST && shValue('NODE_MAJOR') === String(cdp.NODE_MAJOR)
        && guiLive.includes(`\nDT=${live.DT}\n`) && sh.includes(`&dt=${live.DT}&`), 'contract.script_and_runner_agree',
        `web-live.sh: views '${shValue('VIEWS')}', themes '${shValue('THEMES')}', host ${shValue('HOST')}, node `
        + `${shValue('NODE_MAJOR')}; the dt is gui-live.sh's ${live.DT}`);
    // The sample loop as the runner knows it: the repository's file, and what the page asks for and holds it to.
    const loop = readFileSync(join(source, 'web', 'audio', 'loop.wav'));
    const loopSha = createHash('sha256').update(loop).digest('hex');
    const pageSha = (/^export const SAMPLE_SHA256 = '([0-9a-f]{64})';/m
      .exec(readFileSync(join(source, 'web', 'main.js'), 'utf8')) || [])[1];
    const pageUrl = (/^export const SAMPLE_URL = '([^']+)';/m
      .exec(readFileSync(join(source, 'web', 'sample.js'), 'utf8')) || [])[1];
    row(live.AUDIO.bytes === loop.length && live.AUDIO.sha256 === loopSha && live.AUDIO.sha256 === pageSha
        && live.AUDIO.path === pageUrl, 'contract.audio',
        `the runner's ${live.AUDIO.path}, ${live.AUDIO.bytes} bytes, SHA-256 ${live.AUDIO.sha256.slice(0, 12)}...: `
        + `web/audio/loop.wav has ${loop.length} bytes and ${loopSha.slice(0, 12)}...; web/main.js holds `
        + `${String(pageSha).slice(0, 12)}..., and web/sample.js asks for ${pageUrl}`);
    row((statSync(script).mode & 0o111) === 0o111, 'contract.script_is_executable',
        `Scripts/web-live.sh has mode ${(statSync(script).mode & 0o777).toString(8)} (the verify-web-live target `
        + 'runs it)');
  }

  // ---- the summary --------------------------------------------------------------------------------------------------
  {
    const printed = [];
    const t = new live.Tally((line) => printed.push(line));
    const codes = [t.exitCode()];
    t.note('browser: x');
    t.say('  PASS     web.selftest browser: copied, not counted');
    t.frame('EQUAL', 'panel.theme0', 'geometry 1 text 2');
    t.row(true, 'pixels.panel.theme0', 'ok');
    codes.push(t.exitCode());
    t.frame('DIFFERS', 'settings.theme0', 'x');
    t.frame('FAIL', 'settings.theme1', 'y');
    t.row(false, 'scenario', 'z');
    codes.push(t.exitCode());
    row(t.last('/out dir') === 'web-live: 2/5 passed (results in /out dir)' && codes.join() === '2,0,1'
        && printed.length === 7 && printed[2] === 'EQUAL    panel.theme0: geometry 1 text 2'
        && printed[4] === 'DIFFERS  settings.theme0: x' && printed[6] === 'FAIL     web.live scenario: z'
        && t.lines.join('\n') === printed.join('\n'), 'summary.last_line',
        `${t.last('/out dir')}; exit ${codes.join(', ')} for nothing judged, all passed, one failed`);
  }

  // ---- how Chrome is started ----------------------------------------------------------------------------------------
  {
    // A stand-in for the browser: it writes down what it was started with, and ends. Nothing is drawn or played.
    const standIn = join(scratch, 'stand-in chrome');
    const argsFile = `${standIn}.args`;
    writeFileSync(standIn, '#!/bin/sh\nprintf \'%s\\n\' "$@" > "$0.args"\nexit 1\n');
    chmodSync(standIn, 0o755);
    const args = () => (existsSync(argsFile) ? readFileSync(argsFile, 'utf8').split('\n').filter((a) => a !== '') : []);
    const started = async (options = {}) => {
      rmSync(argsFile, { force: true });
      let said = 'it started';
      try {
        await cdp.chrome({ chrome: standIn, gpu: 'swiftshader', flags: [], ...options });
      } catch (e) {
        said = e.message;
      }
      return { said, args: args() };
    };
    const profileOf = (list) => list.filter((a) => a.startsWith('--user-data-dir=')).map((a) => a.slice(16));

    const plain = await started();
    const runs = [plain, await started({ autoplay: false }), await started({ extra: ['--window-position=0,0'] }),
                  await started({ gpu: 'metal' }), await started({ sandbox: false })];
    row(runs.every((r) => r.args.includes('--mute-audio') && r.args.includes('--headless=new')
                          && r.args[r.args.length - 1] === 'about:blank' && profileOf(r.args).length === 1)
        && /Chrome went away \(exit 1\)/.test(plain.said), 'chrome.always_muted_and_headless',
        `${runs.length} ways to start it, each with --mute-audio, --headless=new and one profile; a Chrome that `
        + `ends at once: ${plain.said.split(';')[0]}`);

    const own = join(scratch, 'a profile of the caller');
    mkdirSync(own);
    const kept = await started({ profile: own });
    const [thrown] = profileOf(plain.args);
    row(thrown !== undefined && thrown.startsWith(join(tmpdir(), 'fcmp-chrome-')) && !existsSync(thrown)
        && profileOf(kept.args)[0] === own && existsSync(own), 'chrome.throwaway_profile',
        `its own profile ${thrown} is under the temporary directory and gone afterwards; the caller's is used and `
        + 'kept');

    const swift = ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
    row(cdp.gpuFlags(undefined, 'darwin').join() === '--use-angle=metal' && cdp.gpuFlags(undefined, 'linux').join()
        === swift.join() && cdp.gpuFlags('swiftshader', 'darwin').join() === swift.join()
        && swift.every((f) => plain.args.includes(f)) && runs[3].args.includes('--use-angle=metal')
        && !runs[3].args.includes(swift[1]), 'chrome.gpu_by_platform',
        'ANGLE on Metal on macOS, SwiftShader elsewhere, and either where it is asked for');

    const AUTOPLAY = '--autoplay-policy=no-user-gesture-required';
    process.env.FCMP_WEB_LIVE_NO_SANDBOX = '1';
    const byEnv = await started();
    delete process.env.FCMP_WEB_LIVE_NO_SANDBOX;
    row(plain.args.includes(AUTOPLAY) && !runs[1].args.includes(AUTOPLAY) && !plain.args.includes('--no-sandbox')
        && runs[4].args.includes('--no-sandbox') && byEnv.args.includes('--no-sandbox'), 'chrome.autoplay_and_sandbox',
        'the autoplay switch only where it is asked for; --no-sandbox only by the option or '
        + 'FCMP_WEB_LIVE_NO_SANDBOX=1');

    const nullSink = await started({ extra: [cdp.NULL_SINK] });
    row(cdp.NULL_SINK === '--disable-audio-output' && runs.every((r) => !r.args.includes(cdp.NULL_SINK))
        && nullSink.args.includes(cdp.NULL_SINK) && nullSink.args.includes('--mute-audio'), 'chrome.null_sink',
        `${cdp.NULL_SINK} only where it is asked for (a machine where no AudioContext renders), and muted there too`);

    // The scouts' switches are gone: no variable of the environment adds a flag (the gate's own --gpu does that).
    process.env.SCOUT_GPU = '--use-angle=vulkan';
    process.env.SCOUT_FLAGS = '--user-data-dir=/elsewhere --no-sandbox';
    rmSync(argsFile, { force: true });
    try { await cdp.chrome({ chrome: standIn }); } catch { /* the stand-in ends */ }
    const bare = args();
    delete process.env.SCOUT_GPU;
    delete process.env.SCOUT_FLAGS;
    row(bare.length > 0 && !bare.some((a) => /vulkan|elsewhere|--no-sandbox/.test(a)) && profileOf(bare).length === 1
        && cdp.gpuFlags().every((f) => bare.includes(f)), 'chrome.no_environment_flags',
        `with SCOUT_GPU and SCOUT_FLAGS set, a Chrome started with neither gpu nor flags has the platform's `
        + `${cdp.gpuFlags().join(' ')} and nothing of theirs`);

    // Through the DevTools stand-in: what a typed text sends. A character's key code is the key's on a US keyboard
    // (or 0), never its ASCII code, which is another key's (46 '.' is Delete, 39 "'" ArrowRight, 45 '-' Insert).
    const keysFile = join(scratch, 'keys.jsonl');
    const fetchFile = join(scratch, 'fetch.jsonl');
    const evalsFile = join(scratch, 'evals.jsonl');
    Object.assign(process.env, { FAKE_KEYS: keysFile, FAKE_FETCH: fetchFile, FAKE_EVALS: evalsFile,
                                 FAKE_LEVELS: '[-3.5,null,-70]' });
    const typing = await cdp.chrome({ chrome: devtoolsChrome, gpu: 'swiftshader', answerMs: 5000 });
    for (const name of ['FAKE_KEYS', 'FAKE_FETCH', 'FAKE_EVALS', 'FAKE_LEVELS']) delete process.env[name];
    const tab = await typing.page(null);
    const TYPED = "aZ09 -12.5 dB_A/b:c;d,e'f (x)!\"é";
    await tab.type(TYPED);
    await tab.key('Enter');
    let notAKey = '';
    try { await tab.key('F13'); } catch (e) { notAKey = e.message; }
    const sent = readFileSync(keysFile, 'utf8').trim().split('\n').map((l) => JSON.parse(l));
    const downs = sent.filter((k) => k.type !== 'keyUp');
    const want = { a: ['KeyA', 65], Z: ['KeyZ', 90], 0: ['Digit0', 48], 9: ['Digit9', 57], ' ': ['Space', 32],
                   '-': ['Minus', 189], '.': ['Period', 190], _: ['Minus', 189], '/': ['Slash', 191],
                   ':': ['Semicolon', 186], ';': ['Semicolon', 186], ',': ['Comma', 188], "'": ['Quote', 222],
                   '(': ['Digit9', 57], ')': ['Digit0', 48], '!': ['Digit1', 49], '"': ['Quote', 222], é: ['', 0] };
    const wrongKeys = [...TYPED].map((ch, i) => {
      const d = downs[i] || {};
      const [code, vk] = want[ch] || [d.code, d.windowsVirtualKeyCode];
      return d.key === ch && d.text === ch && d.type === 'keyDown' && d.code === code && d.windowsVirtualKeyCode === vk
        && !(vk >= 33 && vk <= 46) ? '' : `${ch}: ${JSON.stringify(d)}`;
    }).filter((w) => w !== '');
    const enter = downs[[...TYPED].length] || {};
    row(wrongKeys.length === 0 && downs.length === [...TYPED].length + 1 && sent.length === 2 * downs.length
        && enter.windowsVirtualKeyCode === 13 && enter.text === '\r'
        && sent.every((k) => !('nativeVirtualKeyCode' in k))
        && /'F13' is not a key/.test(notAKey), 'chrome.keys',
        wrongKeys.join('; ') || `${JSON.stringify(TYPED)}: each character with its US key's code ('.' Period 190, '-' `
        + `Minus 189, "'" Quote 222, '(' Digit9 57, 'é' none), never 33-46; Enter 13; no nativeVirtualKeyCode; an `
        + 'unknown named key refused');

    // A tab's gate, through the same stand-in: one address answered in the server's place. A request the stand-in
    // stops is answered as the gate says at that moment; one that is held waits for release(); one the page gave up
    // (its answer is refused) is marked; and after end() the gate hears no more.
    const gated = await typing.page(null);
    const door = await gated.gate('*/audio/loop.wav');
    const stops = (id) => gated.ev(`request:${id}`);
    await stops('r1');                                  // the answer a gate begins with: pass
    door.answer = 'missing';
    await stops('r2');
    door.answer = 'fail';
    await stops('r3');
    door.answer = 'hold';
    await stops('r4');
    await stops('gone5');
    await Promise.all(door.requests.slice(0, 3).map((r) => r.done));
    const waiting = door.requests.filter((r) => r.done === null).length;
    const released = await door.release('missing');
    await door.end();
    await stops('r6');
    const fetched = readFileSync(fetchFile, 'utf8').trim().split('\n').map((l) => JSON.parse(l));
    const did = fetched.map((c) => `${c.method.slice(6)} ${c.requestId || ''}`.trim()).join(', ');
    const how = door.requests.map((r) => `${r.id} ${r.how}${r.gone ? ' gone' : ''}`).join(', ');
    row(JSON.stringify(fetched[0]) === JSON.stringify({ method: 'Fetch.enable', patterns: [
          { urlPattern: '*/audio/loop.wav', requestStage: 'Request' }] })
        && did === 'enable, continueRequest r1, fulfillRequest r2, failRequest r3, fulfillRequest r4, '
                   + 'fulfillRequest gone5, disable'
        && fetched[2].responseCode === 404 && fetched[3].errorReason === 'ConnectionRefused'
        && how === 'r1 pass, r2 missing, r3 fail, r4 missing, gone5 missing gone' && waiting === 2
        && released.length === 2 && door.requests[0].url === 'http://site.invalid/audio/loop.wav'
        && door.requests[0].headers.Accept === '*/*', 'chrome.gate',
        `the commands sent: ${did}; the requests: ${how}; ${waiting} waited until release(), and one stopped after `
        + 'end() was not heard');

    // The port tap's count of a reply's columns. The tap is a script for the page: the stand-in kept its text, and
    // it runs here under node, on a port and on replies made here (a head of 320 bytes, then 32 bytes a column: the
    // first float of each is the input's level over one millisecond). The columns kept one by one are read as the
    // stand-in page gave them: digital silence has no number in JSON and comes back as -Infinity.
    await gated.tap();
    await gated.tapHear(2, 3);
    await gated.tapRead();
    const read = await gated.tapLevels();
    const [tapJs, hearJs, readJs, levelsJs] = readFileSync(evalsFile, 'utf8').trim().split('\n').slice(-4)
      .map((l) => JSON.parse(l));
    const hearers = [];
    const port = { postMessage: () => {}, addEventListener: (type, f) => hearers.push(f) };
    const world = { fcmpPage: { node: () => ({ port }) }, performance, ArrayBuffer, Uint16Array, Uint32Array,
                    Float32Array, Array, Object, Number, Math, JSON };
    world.globalThis = world;
    runInNewContext(tapJs, world);
    const tell = (first, levels, count = levels.length) => {
      const data = new ArrayBuffer(320 + 32 * levels.length);
      new Uint32Array(data, 0, 80).set([count, first], 6);
      const columns = new Float32Array(data, 320, levels.length * 8);
      levels.forEach((db, k) => columns.set([db, 99, 99], k * 8));      // a column's other values are not the input's
      for (const hear of hearers) hear({ data });
    };
    const heard = () => JSON.stringify(JSON.parse(runInNewContext(readJs, world)).heard);
    const levels = () => runInNewContext(levelsJs, world);
    const steps = [];
    const step = (what, want) => steps.push({ what, want: JSON.stringify(want), got: heard() });
    step('before any reply', { n: 0, min: null, max: null, lost: 0 });
    tell(100, [-10, -20, -3]);
    step('three columns', { n: 3, min: -20, max: -3, lost: 0 });
    const keptUnasked = levels();                       // nothing asked for them yet: none is kept
    runInNewContext(hearJs, world);                     // begins again, after two columns more, and keeps three
    tell(103, [-1, -2, -30, -6]);
    step('begun again with a skip of two', { n: 2, min: -30, max: -6, lost: 0 });
    const keptTwo = levels();
    tell(107, []);
    tell(107, [-0.5], 0);                               // the head says no column: what lies after it is not read
    step('a reply with no column', { n: 2, min: -30, max: -6, lost: 0 });
    tell(110, [-5]);
    step('three columns not delivered', { n: 3, min: -30, max: -5, lost: 3 });
    tell(50, [-40]);
    step('the count began again', { n: 4, min: -40, max: -5, lost: 4 });
    for (const hear of hearers) {
      hear({ data: new ArrayBuffer(64) });
      hear({ data: { fcmp: 'stats' } });
    }
    step('what is not a reply', { n: 4, min: -40, max: -5, lost: 4 });
    const wrongSteps = steps.filter((x) => x.got !== x.want).map((x) => `${x.what}: ${x.got}, not ${x.want}`);
    const keptAll = levels();                           // the first three after the skip, and not the fourth
    const keptRight = keptUnasked === '[]' && keptTwo === '[-30,-6]' && keptAll === '[-30,-6,-5]'
                      && read.length === 3 && read[0] === -3.5 && read[1] === -Infinity && read[2] === -70;
    row(hearers.length === 1 && wrongSteps.length === 0 && world.__tap.replies === 6 && keptRight, 'chrome.tap_hears',
        wrongSteps.join('; ') || `${steps.map((x) => x.what).join('; ')}: the last count is ${heard()}; kept one `
        + `by one: ${keptUnasked} before any was asked for, then ${keptTwo}, then ${keptAll} (three were asked for); `
        + `the library reads a page's [-3.5,null,-70] as ${read.join(', ')}`);

    // page.until on a page that does not answer (its main thread never rests): it ends at its bound, not at the
    // library's answer timeout.
    await typing.close();
    process.env.FAKE_EVAL = 'never';
    const deaf = await cdp.chrome({ chrome: devtoolsChrome, gpu: 'swiftshader', answerMs: 5000 });
    delete process.env.FAKE_EVAL;
    const deafTab = await deaf.page(null);
    const t0 = Date.now();
    const value = await deafTab.until('document.title', 400);
    const untilMs = Date.now() - t0;
    await deaf.close();
    row(value === null && untilMs >= 400 && untilMs < 2000 && (await ended(typing.pid)) && (await ended(deaf.pid)),
        'chrome.until_bound',
        `until('document.title', 400) on a page that never answers: ${value} after ${untilMs} ms (the answer `
        + 'timeout is 5000 ms)');

    const onPath = join(scratch, 'bin on PATH');
    mkdirSync(onPath);
    copyFileSync(standIn, join(onPath, 'google-chrome'));
    chmodSync(join(onPath, 'google-chrome'), 0o755);
    let none = null;
    try {
      await cdp.chrome({ chrome: join(scratch, 'nowhere', 'chrome') });
    } catch (e) {
      none = e;
    }
    row(none !== null && none.code === 'NO_CHROME' && /^no Chrome \(--chrome /.test(none.message)
        && cdp.findChrome(standIn) === standIn && cdp.findChrome('', { CHROME: standIn, PATH: '' }, 'linux') === standIn
        && cdp.findChrome('', { PATH: scratch }, 'linux') === ''
        && cdp.findChrome('', { PATH: `${scratch}:${onPath}` }, 'linux') === join(onPath, 'google-chrome')
        && cdp.findChrome(join(scratch, 'nowhere'), { CHROME: standIn, PATH: onPath }, 'linux') === '',
        'chrome.found_or_said',
        `no executable: ${none && none.message.slice(0, 22)}...; the path given comes before $CHROME, then `
        + 'google-chrome on PATH; a path that names nothing is never replaced by another browser');

    // The gate itself with that stand-in: no verdict, exit 2; its first Chrome is started without the autoplay
    // switch (a capture page has no AudioContext, and the self-test's first run is the suspended one).
    const out = join(scratch, 'results of a gate');
    rmSync(argsFile, { force: true });
    const gate = spawnSync('/bin/sh', [script, '--dir', site, '--live', liveDir, '--expect', expectDir, '--out', out,
                                       '--chrome', standIn],
                           { encoding: 'utf8', timeout: 60000, cwd: scratch,
                             env: { ...process.env, NODE: process.execPath } });
    const first = args();
    const summary = existsSync(join(out, 'summary.txt')) ? readFileSync(join(out, 'summary.txt'), 'utf8') : '';
    row(gate.status === 2 && /^web-live: no verdict \(Chrome went away/m.test(gate.stdout)
        && !/web-live: \d+\/\d+ passed/.test(gate.stdout) && first.includes('--mute-audio')
        && first.includes('--headless=new') && !first.includes(AUTOPLAY) && first.includes('--window-size=1280,1000')
        && summary.startsWith(`web-live.sh: ${site} (an artifact`) && /^ {2}chrome {6}.*stand-in chrome$/m.test(summary)
        && /^web-live: .* at http:\/\/127\.0\.0\.1:\d+\/, live /m.test(summary), 'gate.no_verdict',
        `exit ${gate.status}: ${(gate.stdout.match(/^web-live: no verdict.*$/m) || ['no such line'])[0].slice(0, 60)}; `
        + 'its Chrome was muted, headless and without the autoplay switch; summary.txt has the script\'s head and '
        + 'the runner\'s lines');

    // --gpu swiftshader: the software renderer for the gate's Chrome on any platform.
    rmSync(argsFile, { force: true });
    const soft = spawnSync('/bin/sh', [script, '--dir', site, '--live', liveDir, '--expect', expectDir, '--out',
                                       join(scratch, 'results of a soft gate'), '--chrome', standIn, '--gpu',
                                       'swiftshader'],
                           { encoding: 'utf8', timeout: 60000, cwd: scratch,
                             env: { ...process.env, NODE: process.execPath } });
    const softArgs = args();
    row(soft.status === 2 && swift.every((f) => softArgs.includes(f)) && !softArgs.includes('--use-angle=metal')
        && /^ {2}gpu {9}swiftshader \(forced/m.test(soft.stdout), 'gate.gpu',
        `--gpu swiftshader: its Chrome had ${softArgs.filter((a) => /angle|swiftshader/.test(a)).join(' ')}; the `
        + 'summary says it was forced');

    // The results directory: a run may only ever remove or replace what the gate wrote. Each listing is every path
    // under a directory with its content, so "untouched" means byte for byte.
    const listing = (dir) => {
      const all = [];
      const walk = (at, rel) => {
        for (const name of readdirSync(at).sort()) {
          const path = join(at, name);
          if (statSync(path).isDirectory()) walk(path, `${rel}${name}/`);
          else all.push(`${rel}${name}=${readFileSync(path, 'utf8')}`);
        }
      };
      if (existsSync(dir)) walk(dir, '');
      return all.join('|');
    };
    const gateInto = (out, more = []) => spawnSync('/bin/sh', [script, '--dir', site, '--live', liveDir, '--expect',
                                                               expectDir, '--out', out, '--chrome', standIn, ...more],
                                                   { encoding: 'utf8', timeout: 60000, cwd: scratch,
                                                     env: { ...process.env, NODE: process.execPath } });
    // A directory that holds something else: refused, twice, and left as it was.
    const other = join(scratch, 'not a results directory');
    put(join(other, 'notes.log'), 'mine\n');
    put(join(other, 'png', 'holiday.png'), 'mine\n');
    put(join(other, 'expect', 'precious.txt'), 'mine\n');
    put(join(other, 'summary.txt'), 'mine\n');
    const before = listing(other);
    const refusedRuns = [gateInto(other), gateInto(other)];
    // One that holds an expect/ directory alone (the artifact as it is downloaded): taken, and expect/ kept.
    const withExpect = join(scratch, 'an artifact results');
    put(join(withExpect, 'expect', 'panel.theme0.node.fp'), 'kept\n');
    const taken = gateInto(withExpect);
    // An earlier run's: its results go before the next run, and nothing else.
    const earlier = join(scratch, 'an earlier run');
    const firstRun = gateInto(earlier);
    put(join(earlier, 'frames', 'old.theme0.live.fp'), 'stale\n');
    put(join(earlier, 'scenario', 'scenario-x', 'old.txt'), 'stale\n');
    for (const name of ['page.log', 'selftest.log', 'selftest.autoplay.log', 'scenario.log']) {
      put(join(earlier, name), 'stale\n');
    }
    put(join(earlier, 'png', 'old.png'), 'stale\n');
    put(join(earlier, 'notes.log'), 'mine\n');
    put(join(earlier, 'expect', 'mine.fp'), 'mine\n');
    const second = gateInto(earlier);
    const after = listing(earlier);
    row(refusedRuns.every((r) => r.status === 2 && /is not a results directory of the gate \(it holds notes\.log, png, /
          .test(r.stderr) && !/web-live: no verdict/.test(r.stdout)) && listing(other) === before
        && taken.status === 2 && /no verdict/.test(taken.stdout) && existsSync(join(withExpect, '.web-live'))
        && readFileSync(join(withExpect, 'expect', 'panel.theme0.node.fp'), 'utf8') === 'kept\n'
        && firstRun.status === 2 && second.status === 2 && existsSync(join(earlier, '.web-live'))
        && !/stale/.test(after) && after.includes('notes.log=mine') && after.includes('expect/mine.fp=mine')
        && /summary\.txt=web-live\.sh: /.test(after), 'gate.results_directory',
        `a directory holding something else: refused twice (${refusedRuns[0].stderr.trim().split('\n')[0]
          .slice(0, 90)}...), byte for byte as it was; one holding only expect/: taken, expect/ kept; an earlier `
        + `run's: its frames, png, scenario and logs gone before the next, notes.log and expect/ kept`);

    // With --dir and no --out the results go beside the site, whatever the current directory is.
    const elsewhere = join(scratch, 'a current directory');
    mkdirSync(elsewhere);
    const besideSite = join(scratch, 'web-live');
    const defaulted = spawnSync('/bin/sh', [script, '--dir', site, '--live', liveDir, '--expect', expectDir,
                                            '--chrome', standIn],
                                { encoding: 'utf8', timeout: 60000, cwd: elsewhere,
                                  env: { ...process.env, NODE: process.execPath } });
    const besideOk = existsSync(join(besideSite, '.web-live')) && existsSync(join(besideSite, 'frames'))
      && readOr(join(besideSite, 'summary.txt')).startsWith(`web-live.sh: ${site} (an artifact`);
    const cwdLeft = readdirSync(elsewhere);
    rmSync(besideSite, { recursive: true, force: true });
    row(defaulted.status === 2 && besideOk && cwdLeft.length === 0, 'gate.default_results',
        `--dir <site> with no --out: the results in ${besideSite.replace(scratch, '<site>/..')}, the current `
        + `directory left empty (${cwdLeft.length} entries)`);

    // The published site: built-from.txt is asked first; a site that never says the commit is exit 1 and no Chrome.
    // Then its sample loop: the row `audio` comes after `published`, and the pages are run whatever it says.
    const NEW = `site abcdef1${'2'.repeat(33)} clean 2026-10-02T00:00:00Z\n`;
    let says = `site ${'1'.repeat(40)} clean 2026-10-01T00:00:00Z\n`;
    const loop = readFileSync(join(source, 'web', 'audio', 'loop.wav'));
    let audio = loop;
    const remote = createServer((req, res) => {
      const body = req.url === '/some/sub/path/built-from.txt' ? says
                 : req.url === '/some/sub/path/audio/loop.wav' ? audio : null;
      res.writeHead(body === null ? 404 : 200);
      res.end(body === null ? '' : body);
    });
    await new Promise((r) => remote.listen(0, '127.0.0.1', r));
    const url = `http://127.0.0.1:${remote.address().port}/some/sub/path/`;
    const viaUrl = (more) => new Promise((done) => {
      rmSync(argsFile, { force: true });
      const child = spawn('/bin/sh', [script, '--url', url, '--expect', expectDir, '--commit', 'abcdef1', '--chrome',
                                      standIn, ...more],
                          { cwd: scratch, env: { ...process.env, NODE: process.execPath } });
      let stdout = '';
      child.stdout.on('data', (c) => { stdout += c; });
      child.stderr.on('data', (c) => { stdout += c; });
      child.on('close', (code) => done({ code, stdout, chromeArgs: args() }));
    });
    const stale = await viaUrl(['--wait', '2', '--out', join(scratch, 'published never')]);
    says = NEW;
    const fresh = await viaUrl(['--wait', '2']);
    audio = loop.subarray(0, 1000);
    const cut = await viaUrl(['--wait', '2', '--out', join(scratch, 'published cut')]);
    remote.close();
    const defaultOut = join(scratch, 'web-live');                      // beside the expectations
    const freshSummary = existsSync(join(defaultOut, 'summary.txt'))
      ? readFileSync(join(defaultOut, 'summary.txt'), 'utf8') : '';
    rmSync(defaultOut, { recursive: true, force: true });
    row(stale.code === 1 && /^NOTE {5}web\.live published: .*not yet 'site abcdef1 clean'/m.test(stale.stdout)
        && /^FAIL {5}web\.live published: .*still says 'site 1{40} clean/m.test(stale.stdout)
        && /^web-live: 0\/1 passed \(results in .*published never\)$/m.test(stale.stdout)
        && stale.chromeArgs.length === 0 && !/web\.live audio/.test(stale.stdout)
        && fresh.code === 2
        && /^PASS {5}web\.live published: .*says 'site abcdef12{33} clean.*\nPASS {5}web\.live audio: /m
          .test(fresh.stdout)
        && /^PASS {5}web\.live audio: .*\/some\/sub\/path\/audio\/loop\.wav: 2048600 bytes/m.test(fresh.stdout)
        && /web-live: no verdict \(Chrome went away/.test(fresh.stdout) && fresh.chromeArgs.includes('--mute-audio')
        && freshSummary.startsWith(`web-live.sh: ${url} (the published site`)
        && cut.code === 2 && /^PASS {5}web\.live published: .*\nFAIL {5}web\.live audio: .*: 1000 bytes, /m
          .test(cut.stdout) && /web-live: no verdict \(Chrome went away/.test(cut.stdout)
        && cut.chromeArgs.includes('--mute-audio'), 'gate.url_form',
        `a site that never says the commit: exit ${stale.code}, no Chrome started, no audio row, `
        + `'${(/^web-live: \d+\/\d+ passed.*$/m.exec(stale.stdout) || [''])[0].replace(scratch, '.')}'; one that `
        + `does: PASS published, PASS audio, then the pages (exit ${fresh.code} with the stand-in), results beside `
        + 'the expectations; one whose sample loop is cut to 1000 bytes: FAIL audio, and the pages all the same '
        + `(its Chrome was started, exit ${cut.code})`);
  }

  // ---- what the script runs -----------------------------------------------------------------------------------------
  {
    // A stand-in for node: it says it is node 24, names a browser when asked to find one, hands the results
    // directory's preparation to the real node (the guard is the runner's), writes down every other call with the two
    // preference variables, and writes the file after --fp as ui.dump would. $FAKE_FAILS names an argument at whose
    // sight it fails instead (exit 3); with $FAKE_SLOW (a path) a ui.dump touches it and sleeps.
    const fake = join(scratch, 'stand-in node');
    const calls = `${fake}.log`;
    writeFileSync(fake, `#!/bin/sh
case "\${1:-}" in --version) echo v24.0.0; exit 0 ;; esac
for a in "$@"; do
  if [ "$a" = --find-chrome ]; then echo "/a browser/chrome"; echo "FIND" >> "$0.log"; exit 0; fi
  if [ "$a" = --prepare-out ]; then
    { echo "PREP"; printf 'ARG %s\n' "$@"; } >> "$0.log"
    exec '${process.execPath}' "$@"
  fi
  if [ "$a" = ui.dump ] && [ -n "\${FAKE_SLOW:-}" ]; then : > "$FAKE_SLOW"; sleep 30; fi
done
{ echo "RUN \${FCMP_PREFS_DIR:-}|\${FCMP_PRESETS_DB:-}"; printf 'ARG %s\n' "$@"; } >> "$0.log"
prev=""
for a in "$@"; do
  [ -n "\${FAKE_FAILS:-}" ] && [ "$a" = "\${FAKE_FAILS:-}" ] && exit 3
  [ "$prev" = --fp ] && printf 'geometry 0\ntext 0\n' > "$a"
  prev=$a
done
exit 0
`);
    chmodSync(fake, 0o755);
    // The calls as [{ env, args }] (the results directory's preparation as `preps`), and how often a browser was
    // looked for.
    const ran = (args, env = {}) => {
      rmSync(calls, { force: true });
      const r = spawnSync('/bin/sh', [script, ...args], { encoding: 'utf8', timeout: 60000, cwd: scratch,
                                                         env: { ...process.env, NODE: fake, CHROME: '', ...env } });
      const text = existsSync(calls) ? readFileSync(calls, 'utf8') : '';
      const blocks = text.split(/^(?=RUN |PREP$)/m).filter((b) => /^(RUN |PREP)/.test(b)).map((block) => {
        const lines = block.split('\n');
        return { prep: lines[0] === 'PREP', env: lines[0].replace(/^RUN /, '').split('|'),
                 args: lines.slice(1).filter((l) => l.startsWith('ARG ')).map((l) => l.slice(4)) };
      });
      return { code: r.status, out: r.stdout || '', err: r.stderr || '', runs: blocks.filter((b) => !b.prep),
               preps: blocks.filter((b) => b.prep).map((b) => b.args), finds: (text.match(/^FIND$/gm) || []).length,
               order: blocks.map((b) => (b.prep ? 'PREP' : 'RUN')).join(' ') };
    };
    const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

    // A web build tree whose node is the stand-in, of a source that has the expectation tool.
    const src = join(scratch, 'a source');
    const tool = join(src, 'Tools', 'web', 'live', 'expect.mjs');
    put(tool, '');
    const tree = join(scratch, 'build web');
    put(join(tree, 'CMakeCache.txt'), `CMAKE_HOME_DIRECTORY:INTERNAL=${src}\nFCOMPRESSOR_WEB:BOOL=ON\n`
        + `CMAKE_CROSSCOMPILING_EMULATOR:FILEPATH=${fake}\n`);
    put(join(tree, 'site', 'built-from.txt'), 'site 0 clean now\n');
    put(join(tree, 'live', 'page.html'), '');
    put(join(tree, 'fcmp_probe_web.js'), '');
    const results = join(tree, 'web-live');
    const expectOf = join(results, 'expect');
    const runnerCall = (more) => [runner, '--dir', join(tree, 'site'), '--live', join(tree, 'live'), '--expect',
                                  expectOf, ...more];

    const built = ran(['build web', '--timeout', '7']);
    const dumps = built.runs.slice(0, 12);
    const wrongDumps = [];
    live.VIEWS.forEach((view, v) => live.THEMES.forEach((theme, t) => {
      const call = dumps[v * live.THEMES.length + t] || { env: [], args: [] };
      const want = [join(tree, 'fcmp_probe_web.js'), 'ui.dump', '--mode', 'clean', '--golden-dir',
                    join(src, 'tests', 'golden'), '--out', call.args[7], '--', '--view', view, '--dpi', '2', '--theme',
                    String(theme), '--facade', 'web', '--host', live.HOST, '--nolive', '1', '--fp',
                    join(expectOf, `${view}.theme${theme}.node.fp`)];
      if (!same(call.args, want)) wrongDumps.push(`${view}.theme${theme}: ${call.args.join(' ')}`);
    }));
    const prefs = [...new Set(dumps.flatMap((call) => call.env))];
    row(built.code === 0 && dumps.length === 12 && wrongDumps.length === 0 && prefs.length === 2
        && prefs.every((path) => /fcmp-web-live\.[^/]+\/(prefs|presets\.db)$/.test(path)
                                 && !existsSync(join(path, '..')))
        && /^ {2}expect {6}12 of 12 node values in /m.test(built.out)
        && readFileSync(join(results, 'summary.txt'), 'utf8').includes('12 of 12 node values'),
        'script.node_values', wrongDumps.join('; ') || `exit ${built.code}: twelve ui.dump calls, each the contract's, `
        + 'with FCMP_PREFS_DIR and FCMP_PRESETS_DB in a scratch directory that is gone afterwards');

    const toolCall = built.runs[12] || { args: [] };
    row(same(toolCall.args, [tool, '--site', join(tree, 'site'), '--live', join(tree, 'live'), '--out', expectOf]),
        'script.expect_tool', `after the node values: ${toolCall.args.slice(1).join(' ').replaceAll(scratch, '.')}`);

    const last = built.runs[13] || { args: [] };
    const artifact = ran(['--dir', 'the site', '--live', 'the live', '--expect', 'the expect', '--out', 'the out',
                          '--timeout', '9', '--chrome', 'my chrome']);
    const serving = ran(['--serve', 'build web']);
    const published = ran(['--url', 'https://example.invalid/a base/', '--expect', 'the expect', '--commit',
                           'abcdef1', '--wait', '30', '--timeout', '9', '--gpu', 'swiftshader']);
    const publishedHead = existsSync(join(scratch, 'web-live', 'summary.txt'))
      ? readFileSync(join(scratch, 'web-live', 'summary.txt'), 'utf8') : '';
    rmSync(join(scratch, 'web-live'), { recursive: true, force: true });
    const wantArtifact = [runner, '--dir', site, '--live', liveDir, '--expect', expectDir, '--out',
                          join(scratch, 'the out'), '--timeout', '9', '--gpu', 'default', '--chrome',
                          '/a browser/chrome', '--keep-summary'];
    const wantPublished = [runner, '--url', 'https://example.invalid/a base/', '--expect', expectDir, '--commit',
                           'abcdef1', '--wait', '30', '--out', join(scratch, 'web-live'), '--timeout', '9', '--gpu',
                           'swiftshader', '--chrome', '/a browser/chrome', '--keep-summary'];
    const prepOf = (out, ...more) => [runner, '--prepare-out', out, ...more];
    row(built.runs.length === 14 && built.finds === 1 && built.order.startsWith('PREP RUN')
        && same(built.preps, [prepOf(results, '--live', join(tree, 'live'), '--with-expect')])
        && same(last.args, runnerCall(['--out', results, '--timeout', '7', '--gpu', 'default', '--chrome',
                                       '/a browser/chrome', '--keep-summary']))
        && artifact.code === 0 && artifact.finds === 1 && artifact.runs.length === 1
        && same(artifact.preps, [prepOf('the out', '--live', liveDir)]) && same(artifact.runs[0].args, wantArtifact)
        && serving.code === 0 && serving.finds === 0 && serving.runs.length === 14
        && same(serving.preps, [prepOf(results, '--live', join(tree, 'live'), '--with-expect')])
        && same(serving.runs[13].args, [runner, '--serve', ...runnerCall([]).slice(1)])
        && published.code === 0 && published.finds === 1 && published.runs.length === 1
        && same(published.preps, [prepOf(join(scratch, 'web-live'))]) && same(published.runs[0].args, wantPublished)
        && publishedHead.startsWith('web-live.sh: https://example.invalid/a base/ (the published site')
        && !/built-from/.test(publishedHead), 'script.runner_arguments',
        `a build: the results directory prepared first, then ${last.args.slice(1).join(' ').replaceAll(scratch, '.')}; `
        + `an artifact: nothing computed, ${artifact.runs.length} call; the published site: `
        + `${published.runs[0] ? published.runs[0].args.slice(1, 9).join(' ') : 'no call'}...; --serve: no browser `
        + 'looked for');

    const oneFails = ran(['build web'], { FAKE_FAILS: 'settings' });
    const kept = ['settings.theme1.node.fp', 'settings.theme1.node.log', 'panel.theme0.node.fp',
                  'panel.theme0.node.log'].map((name) => existsSync(join(expectOf, name)));
    const toolFails = ran(['build web'], { FAKE_FAILS: '--site' });
    row(oneFails.code === 0 && /no node value for settings\.theme0 \(ui\.dump exit 3\)/.test(oneFails.err)
        && /^ {2}expect {6}10 of 12 node values in /m.test(oneFails.out)
        && same(kept, [false, true, true, false]) && oneFails.runs.length === 14
        && toolFails.code === 2 && /the expectation tool failed \(exit 3\)/.test(toolFails.err)
        && toolFails.runs.length === 13, 'script.failures',
        `a ui.dump that fails leaves no expectation and its output (${oneFails.err.trim().split('\n').length} said), `
        + `and the runner still runs; the expectation tool failing ends the gate: exit ${toolFails.code}`);

    // A signal while the node values are made (a terminal closed, a job cancelled): exit 2, and the scratch
    // directory of the preferences is gone, under each shell there is (dash runs no EXIT trap for a signal it does
    // not catch).
    const shells = ['/bin/sh', '/bin/dash'].filter((shell) => existsSync(shell));
    const signalled = [];
    for (const shell of shells) {
      for (const signal of ['SIGINT', 'SIGTERM', 'SIGHUP']) {
        const tmp = join(scratch, `tmp ${basename(shell)} ${signal}`);
        mkdirSync(tmp, { recursive: true });
        const started = join(tmp, 'a ui.dump started');
        const child = spawn(shell, [script, 'build web'], { cwd: scratch, detached: true, stdio: 'ignore',
                                                            env: { ...process.env, NODE: fake, CHROME: '',
                                                                   FAKE_SLOW: started, TMPDIR: tmp } });
        const closed = new Promise((r) => child.on('close', (code, sig) => r(code ?? sig)));
        for (const t0 = Date.now(); !existsSync(started) && Date.now() - t0 < 20000;) await pause(50);
        const was = readdirSync(tmp).filter((n) => n.startsWith('fcmp-web-live.')).length;
        try { process.kill(-child.pid, signal); } catch { /* gone */ }
        const code = await Promise.race([closed, pause(20000).then(() => 'still running')]);
        if (code === 'still running') { try { process.kill(-child.pid, 'SIGKILL'); } catch { /* gone */ } }
        const left = readdirSync(tmp).filter((n) => n.startsWith('fcmp-web-live.'));
        signalled.push({ ok: was === 1 && code === 2 && left.length === 0,
                         text: `${basename(shell)} ${signal}: exit ${code}, scratch ${left.length ? 'LEFT' : 'gone'}`,
                       });
      }
    }
    row(shells.length > 0 && signalled.every((s) => s.ok), 'script.signals',
        `during the node values: ${signalled.map((s) => s.text).join('; ')}`);
  }

  // ---- usage --------------------------------------------------------------------------------------------------------
  {
    // No row here may start a browser: $CHROME names nothing, so a refusal that failed would end at "no Chrome".
    const noChrome = join(scratch, 'nowhere', 'chrome');
    const run = (cmd, args, env = {}) => {
      const r = spawnSync(cmd, args, { encoding: 'utf8', timeout: 60000, cwd: scratch,
                                       env: { ...process.env, NODE: process.execPath, CHROME: noChrome, ...env } });
      return { code: r.status, out: r.stdout || '', err: r.stderr || '' };
    };
    const sh = (args, env) => run('/bin/sh', [script, ...args], env);
    const refused = (r, reason) => r.code === 2 && reason.test(r.err) && !/web-live: \d+\/\d+ passed/.test(r.out);
    const say = (r) => `exit ${r.code}: ${r.err.trim().split('\n')[0]}`;

    const dirArgs = ['--dir', site, '--live', liveDir, '--expect', expectDir];
    const URL_ = 'http://127.0.0.1:9/some/sub/path/';
    const cases = {
      no_arguments: [sh([]), /a build directory, --dir <site> --live <dir> --expect <dir>, or --url/],
      unknown_option: [sh(['--bogus', build]), /unknown option --bogus/],
      missing_value: [sh([build, '--out']), /--out needs a value/],
      empty_value: [sh([build, '--chrome', '']), /--chrome needs a value/],
      bad_timeout: [sh([build, '--timeout', '0']), /--timeout is a whole number/],
      bad_timeout_word: [sh([build, '--timeout', 'soon']), /--timeout is a whole number/],
      two_builds: [sh([build, build]), /one build directory, not two/],
      both_forms: [sh([build, ...dirArgs]), /not both/],
      dir_alone: [sh(['--dir', site]), /--dir needs --live <dir> and --expect <dir>/],
      live_without_dir: [sh(['--live', liveDir, '--expect', expectDir]), /a build directory, --dir <site>/],
      no_such_build: [sh([join(scratch, 'nowhere')]), /no build directory/],
      not_configured: [sh([liveDir]), /is not a configured build directory/],
      no_such_site: [sh(['--dir', join(scratch, 'nowhere'), '--live', liveDir, '--expect', expectDir]),
                     /no site directory/],
      site_without_built_from: [sh(['--dir', liveDir, '--live', liveDir, '--expect', expectDir]),
                                /is not a built site \(no built-from\.txt\)/],
      no_live_directory: [sh(['--dir', site, '--live', join(scratch, 'nowhere'), '--expect', expectDir]),
                          /no live directory/],
      no_expectations: [sh(['--dir', site, '--live', liveDir, '--expect', join(scratch, 'nowhere')]),
                        /no expectation directory/],
      bad_gpu: [sh([build, '--gpu', 'metal']), /--gpu is default or swiftshader, not 'metal'/],
      commit_without_url: [sh([...dirArgs, '--commit', 'abcdef1']), /--commit and --wait go with --url/],
      wait_without_url: [sh([build, '--wait', '5']), /--commit and --wait go with --url/],
      url_with_dir: [sh(['--url', URL_, '--expect', expectDir, '--dir', site]),
                     /--url is the published site: not with a build directory, --dir or --live/],
      url_with_build: [sh([build, '--url', URL_, '--expect', expectDir]), /not with a build directory/],
      url_without_expect: [sh(['--url', URL_]), /--url needs --expect <dir>/],
      url_not_http: [sh(['--url', 'ftp://127.0.0.1/site/', '--expect', expectDir]), /http or https address/],
      url_with_query: [sh(['--url', `${URL_}?view=panel`, '--expect', expectDir]), /no query and no fragment/],
      url_no_expectations: [sh(['--url', URL_, '--expect', join(scratch, 'nowhere')]), /no expectation directory/],
      url_bad_commit: [sh(['--url', URL_, '--expect', expectDir, '--commit', 'ABCDEF1']), /--commit is 7 to 40/],
      url_short_commit: [sh(['--url', URL_, '--expect', expectDir, '--commit', 'abc12']), /--commit is 7 to 40/],
      url_bad_wait: [sh(['--url', URL_, '--expect', expectDir, '--wait', 'soon']), /--wait is a whole number/],
      serve_url: [sh(['--serve', '--url', URL_, '--expect', expectDir]), /--serve serves a site of this machine/],
    };
    // A tree that is configured, but not for the web; and a web tree with nothing built in it.
    const native = join(scratch, 'build native');
    put(join(native, 'CMakeCache.txt'), `CMAKE_HOME_DIRECTORY:INTERNAL=${source}\nFCOMPRESSOR_WEB:BOOL=OFF\n`);
    cases.not_a_web_build = [sh([native]), /is not a web build/];
    const bare = join(scratch, 'build bare');
    put(join(bare, 'CMakeCache.txt'), `CMAKE_HOME_DIRECTORY:INTERNAL=${source}\nFCOMPRESSOR_WEB:BOOL=ON\n`
        + `CMAKE_CROSSCOMPILING_EMULATOR:FILEPATH=${process.execPath}\n`);
    cases.no_built_site = [sh([bare]), /is not a built site \(no built-from\.txt\)/];
    // No node, and a node that is too old (a stand-in that says so).
    const oldNode = join(scratch, 'old node');
    writeFileSync(oldNode, '#!/bin/sh\necho v18.20.4\n');
    chmodSync(oldNode, 0o755);
    cases.no_node = [sh(dirArgs, { NODE: join(scratch, 'nowhere', 'node') }), /node 22 or later is needed.*not a node/];
    cases.old_node = [sh(dirArgs, { NODE: oldNode }), /node 22 or later is needed.*\(node 18\)/];
    // No Chrome: through --chrome, through $CHROME, and on the real build tree.
    cases.no_chrome = [sh([...dirArgs, '--chrome', noChrome]), /no Chrome \(--chrome /];
    cases.no_chrome_env = [sh(dirArgs, { CHROME: noChrome }), /no Chrome \(\$CHROME /];
    cases.no_chrome_build = [sh([build, '--chrome', noChrome, '--out', join(scratch, 'never made')]),
                             /no Chrome \(--chrome /];
    cases.runner_find_no_chrome = [run(process.execPath, [runner, '--find-chrome', '--chrome', noChrome]),
                                   /no Chrome \(--chrome /];
    cases.runner_no_arguments = [run(process.execPath, [runner]), /--dir is required/];
    cases.runner_unknown = [run(process.execPath, [runner, '--bogus']), /unknown argument --bogus/];
    cases.runner_no_out = [run(process.execPath, [runner, ...dirArgs]), /--out is required/];
    cases.runner_no_chrome = [run(process.execPath, [runner, ...dirArgs, '--out', join(scratch, 'never made'),
                                                     '--chrome', noChrome]), /no Chrome \(--chrome /];
    cases.runner_no_site = [run(process.execPath, [runner, '--dir', liveDir, '--live', liveDir, '--expect', expectDir,
                                                   '--out', join(scratch, 'never made')]), /is not a site/];
    const never = join(scratch, 'never made');
    const node = (args) => run(process.execPath, [runner, ...args]);
    cases.runner_bad_gpu = [node([...dirArgs, '--out', never, '--gpu', 'vulkan']), /--gpu is default or swiftshader/];
    cases.runner_commit_without_url = [node([...dirArgs, '--out', never, '--commit', 'abcdef1']), /go with --url/];
    cases.runner_url_no_out = [node(['--url', URL_, '--expect', expectDir]), /--out is required/];
    cases.runner_url_with_dir = [node(['--url', URL_, '--dir', site, '--expect', expectDir, '--out', never]),
                                 /not with --dir, --live or --serve/];
    cases.runner_url_not_http = [node(['--url', 'file:///etc/', '--expect', expectDir, '--out', never]),
                                 /http or https address/];
    cases.runner_url_bad_commit = [node(['--url', URL_, '--expect', expectDir, '--out', never, '--commit', 'xyz']),
                                   /--commit is 7 to 40/];
    cases.runner_bad_wait = [node(['--url', URL_, '--expect', expectDir, '--out', never, '--wait', '-1']),
                             /--wait is a number of seconds/];
    const foreign = join(scratch, 'a directory of mine');
    put(join(foreign, 'mine.txt'), 'mine\n');
    cases.runner_out_refused = [node(['--prepare-out', foreign]),
                                /is not a results directory of the gate \(it holds mine\.txt/];
    cases.runner_gate_out_refused = [node([...dirArgs, '--out', foreign, '--chrome', process.execPath]),
                                     /is not a results directory of the gate/];
    for (const [name, [r, reason]] of Object.entries(cases)) row(refused(r, reason), `usage.${name}`, say(r));
    row(!existsSync(join(scratch, 'never made')) && !existsSync(join(scratch, 'web-live'))
        && readdirSync(foreign).join() === 'mine.txt' && readFileSync(join(foreign, 'mine.txt'), 'utf8') === 'mine\n',
        'usage.nothing_written', 'a run that is refused leaves no results directory, and a directory refused as one '
        + 'is left as it was');

    // --help is the header: every form, every option the script takes (read from its own argument loop) and each
    // exit code.
    const help = sh(['--help']);
    const options = [...new Set((readFileSync(script, 'utf8').match(/^ {4}(-[-|a-z]+)\)/gm) || [])
      .flatMap((m) => m.trim().slice(0, -1).split('|')))];
    const valued = ((/^ {4}(--dir\|[-|a-z]+)\)/m.exec(readFileSync(script, 'utf8')) || [])[1] || '').split('|');
    const undocumented = [...options.filter((o) => !new RegExp(`(^|[\\s(])${o}\\b`, 'm').test(help.out)),
                          ...valued.filter((o) => !help.out.includes(`${o} <`)).map((o) => `${o} <value>`)];
    const forms = ['<build-web> [options]', '--dir <site> --live <dir> --expect <dir> [options]',
                   '--url <base> --expect <dir> [--commit <sha>] [--wait <s>] [options]', '--serve <build-web>',
                   '--serve --dir <site> --live <dir> --expect <dir>']
      .filter((f) => !help.out.includes(`web-live.sh ${f}`));
    row(help.code === 0 && options.length >= 12 && valued.length >= 10 && undocumented.length === 0
        && forms.length === 0
        && /^Exit: 0 every row passed; 1 .*\n2 usage, .*no Chrome, the results directory refused, no verdict, a signal/m
          .test(help.out) && help.err === '', 'usage.help',
        undocumented.length || forms.length ? `not in --help: ${[...undocumented, ...forms].join(', ')}`
          : `exit ${help.code}, ${help.out.split('\n').length} lines: the five forms, all ${options.length} options `
            + `(${options.join(' ')}) and the exit codes`);
    const found = run(process.execPath, [runner, '--find-chrome', '--chrome', process.execPath]);
    row(found.code === 0 && found.out.trim() === process.execPath, 'usage.find_chrome',
        `--find-chrome prints the executable it was given and nothing else: ${found.out.trim()}`);
  }

  // ---- --serve ------------------------------------------------------------------------------------------------------
  {
    // From another directory, with relative paths that have spaces in them.
    const serving = async () => {
      const child = spawn('/bin/sh', [script, '--serve', '--dir', 'the site', '--live', 'the live', '--expect',
                                      'the expect'],
                          { cwd: scratch, stdio: ['ignore', 'pipe', 'pipe'],
                            env: { ...process.env, NODE: process.execPath, CHROME: join(scratch, 'nowhere') } });
      process.on('exit', () => child.kill('SIGKILL'));   // however this test ends, the server does not outlive it
      const s = { child, out: '', err: '', up: false, base: '' };
      s.ended = new Promise((r) => child.on('close', (code, signal) => r(signal || code)));
      s.up = await new Promise((r) => {
        child.stdout.on('data', (c) => { s.out += c; if (/a fresh origin/.test(s.out)) r(true); });
        child.stderr.on('data', (c) => { s.err += c; });
        s.ended.then(() => r(false));
        setTimeout(() => r(false), 30000);
      });
      s.base = (/serving at (http:\/\/127\.0\.0\.1:\d+)\//.exec(s.out) || [])[1] || '';
      // The signal sent, then how the process ended and what the server answers afterwards.
      s.end = async (signal) => {
        child.kill(signal);
        const code = await Promise.race([s.ended, new Promise((r) => setTimeout(() => r('still running'), 10000))]);
        if (code === 'still running') child.kill('SIGKILL');
        const gone = await ask(s.base, '/index.html');
        return { code, gone: gone.status === 0, said: `${signal}: exit ${code}, and the server answers `
                                                      + `${gone.error || gone.status}` };
      };
      return s;
    };
    const first = await serving();
    const { base, out } = first;
    if (!row(first.up && base !== '', 'serve.starts',
             first.up ? `${base}, with no Chrome to be found: --serve needs none`
                      : `it did not come up: ${(first.err || out).trim().slice(0, 300)}`)) {
      first.child.kill('SIGKILL');
    } else {
      const lines = out.split('\n');
      const missing = [];
      live.VIEWS.forEach((view, i) => {
        const at = lines.indexOf(`  ${live.captureUrl(base, view, 0)}`);
        const values = `geometry ${`${i}`.repeat(16)} text ${'f'.repeat(16)}`;
        if (at < 0 || lines[at + 1].trim() !== values) missing.push(view);
      });
      row(missing.length === 0 && lines.includes(`the page:       ${base}/`)
          && lines.some((l) => l.startsWith(`the self-test:  ${base}/index.html?selftest=1`))
          && lines.includes(`  ${base}/live/page.html`), 'serve.urls',
          missing.length ? `no capture URL with its expected values for ${missing.join(', ')}`
            : 'the page, the self-test, six capture pages with their geometry and text, the live page');
      const got = [await ask(base, '/index.html?selftest=1'), await ask(base, '/live/page.html'),
                   await ask(base, '/expect/panel.theme0.node.fp'), await ask(base, '/fcmp-ui.wasm'),
                   await ask(base, new URL(live.captureUrl(base, 'settings', 0)).pathname
                                   + new URL(live.captureUrl(base, 'settings', 0)).search)];
      row(got.every((r) => r.status === 200) && got[3].headers['content-type'] === 'application/wasm'
          && got[4].body.toString().includes('<title>site</title>'), 'serve.answers',
          'the site, the live page, an expectation, the module and a capture page: '
          + got.map((r) => r.status).join(', '));
      const interrupted = await first.end('SIGINT');
      row(interrupted.code === 2 && interrupted.gone, 'serve.interrupted', interrupted.said);
      // The other signals a runner is ended by: the same end.
      const ends = [];
      for (const signal of ['SIGTERM', 'SIGHUP']) {
        const again = await serving();
        if (!again.up) again.child.kill('SIGKILL');
        ends.push(again.up ? await again.end(signal) : { code: 'never up', gone: false, said: `${signal}: never up` });
      }
      row(ends.every((e) => e.code === 2 && e.gone), 'serve.other_signals', ends.map((e) => e.said).join('; '));
    }
  }
} finally {
  removeScratch();
}

console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
