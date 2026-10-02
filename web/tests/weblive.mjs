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
//                behind a prefix, a link that points out), and a server that is gone once killed
//   compare.*    the browser's frame against the node value: equal, a differing line, a missing and an extra line,
//                `live` and `hooks` left out, no expectation (a FAIL, never a pass), and each way a hooks line is wrong
//   pixels.*     the pixel rule by renderer class, on both sides of each bound
//   page.*       a page's end by the self-test's protocol: no verdict within the timeout, a FAIL title, and a PASS
//                title that the log or an uncaught error contradicts
//   scenario.*   the scenario's end by its contract: the last line, the exit code, a run that never ended
//   contract.*   the capture page's address is the contract's, its pins are names Source/web/ui/WebMain.cpp reads,
//                the views are the editor's, and web-live.sh computes node values for the views the runner opens
//   summary.*    the count, the last line and the exit code of a run
//   chrome.*     how the library starts Chrome, seen by a stand-in executable that writes down its arguments and
//                ends: always --mute-audio and --headless=new, a throwaway profile that is gone afterwards, the GPU
//                flag by platform, the autoplay, sandbox and null-sink switches only where asked, a missing
//                browser said
//   gate.*       web-live.sh with that stand-in: no verdict, exit 2, its first Chrome without the autoplay switch;
//                what a run removes from its results directory
//   script.*     what web-live.sh runs, seen by a stand-in for node: the twelve ui.dump calls of the contract with
//                scratch preference paths, the expectation tool, the runner's arguments for a build tree, an
//                artifact and --serve, and what a failing ui.dump or tool leads to
//   usage.*      every way web-live.sh and live.mjs refuse to run, each with exit 2 and its reason: no arguments, an
//                unknown option, a missing value, both forms at once, no such directory, not a build tree, not a web
//                build, no built site, --dir without --live and --expect, a site without built-from.txt, no node,
//                a node older than 22, no Chrome (on the real build tree too); and --help
//   serve.*      web-live.sh --serve --dir from another directory, every path with a space in it: the URLs of the
//                contract with what each capture page must give, the three roots answering, and SIGINT, SIGTERM
//                and SIGHUP each ending it
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { spawn, spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, realpathSync, rmSync, statSync,
         symlinkSync, writeFileSync } from 'node:fs';
import { request } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

const TEST = 'web.live.runner';

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
put(join(liveDir, 'page.html'), '<!doctype html><title>RUNNING</title>');
put(join(liveDir, 'golden', 'clean.dsp.print.txt'), 'rows\n');
live.VIEWS.forEach((view, i) => put(join(expectDir, `${view}.theme0.node.fp`), FP(`${i}`.repeat(16), 'f'.repeat(16))));
put(join(expectDir, 'rows.json'), '{}\n');
symlinkSync(join(scratch, 'secret.txt'), join(site, 'link.txt'));          // a link that points out of the root

// One request with the path sent as it is written (no client-side normalising).
const ask = (base, path, method = 'GET') => new Promise((answered) => {
  const url = new URL(base);
  const req = request({ host: url.hostname, port: url.port, path, method, agent: false }, (res) => {
    const chunks = [];
    res.on('data', (c) => chunks.push(c));
    res.on('end', () => answered({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
  });
  req.on('error', (e) => answered({ status: 0, headers: {}, body: Buffer.alloc(0), error: e.code || e.message }));
  req.end();
});

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
                                ['/expect/panel.theme0.node.fp', 'text/plain']]) {
      const r = await get(path);
      if (r.status !== 200 || !String(r.headers['content-type']).startsWith(type)) {
        types.push(`${path}: ${r.status} ${r.headers['content-type']}`);
      }
    }
    row(types.length === 0, 'server.types', types.join('; ') || 'html, js, txt, json and fp have their types');

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
    row(live.isVerdict('PASS') && live.isVerdict('FAIL: x') && live.isVerdict('FAIL') && !live.isVerdict('RUNNING')
        && !live.isVerdict('PASSED') && !live.isVerdict(''), 'page.is_verdict',
        'PASS and FAIL... are verdicts; RUNNING, PASSED and an empty title are not');
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

    // What a run removes from --out: an earlier run's results (a directory that has frames/), and nothing from a
    // directory that holds something else.
    const other = join(scratch, 'not a results directory');
    put(join(other, 'notes.log'), 'kept\n');
    put(join(other, 'png', 'holiday.png'), 'kept\n');
    const into = () => spawnSync('/bin/sh', [script, '--dir', site, '--live', liveDir, '--expect', expectDir, '--out',
                                             other, '--chrome', standIn],
                                 { encoding: 'utf8', timeout: 60000, cwd: scratch,
                                   env: { ...process.env, NODE: process.execPath } });
    const there = (...names) => names.map((name) => existsSync(join(other, name)));
    const once = into();
    const untouched = there('notes.log', 'png/holiday.png', 'frames', 'summary.txt');
    put(join(other, 'frames', 'old.theme0.live.fp'), 'stale\n');
    put(join(other, 'old-page.log'), 'stale\n');
    put(join(other, 'scenario', 'old.txt'), 'stale\n');
    const twice = into();
    const stale = there('frames/old.theme0.live.fp', 'old-page.log', 'scenario', 'png/holiday.png', 'frames');
    row(once.status === 2 && twice.status === 2 && untouched.every((is) => is)
        && stale.join() === 'false,false,false,false,true', 'gate.results_directory',
        `a directory with something else in it: kept ${untouched.filter((is) => is).length} of 4; an earlier run's `
        + `results: ${stale.slice(0, 4).filter((is) => !is).length} of 4 removed before the next run`);
  }

  // ---- what the script runs -----------------------------------------------------------------------------------------
  {
    // A stand-in for node: it says it is node 24, names a browser when asked to find one, writes down every other
    // call with the two preference variables, and writes the file after --fp as ui.dump would. $FAKE_FAILS names an
    // argument at whose sight it fails instead (exit 3).
    const fake = join(scratch, 'stand-in node');
    const calls = `${fake}.log`;
    writeFileSync(fake, `#!/bin/sh
case "\${1:-}" in --version) echo v24.0.0; exit 0 ;; esac
for a in "$@"; do
  if [ "$a" = --find-chrome ]; then echo "/a browser/chrome"; echo "FIND" >> "$0.log"; exit 0; fi
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
    // The calls as [{ env, args }], and how often a browser was looked for.
    const ran = (args, env = {}) => {
      rmSync(calls, { force: true });
      const r = spawnSync('/bin/sh', [script, ...args], { encoding: 'utf8', timeout: 60000, cwd: scratch,
                                                         env: { ...process.env, NODE: fake, CHROME: '', ...env } });
      const text = existsSync(calls) ? readFileSync(calls, 'utf8') : '';
      const runs = text.split(/^RUN /m).slice(1).map((block) => {
        const lines = block.split('\n');
        return { env: lines[0].split('|'),
                 args: lines.slice(1).filter((l) => l.startsWith('ARG ')).map((l) => l.slice(4)) };
      });
      return { code: r.status, out: r.stdout || '', err: r.stderr || '', runs,
               finds: (text.match(/^FIND$/gm) || []).length };
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
    const wantArtifact = [runner, '--dir', site, '--live', liveDir, '--expect', expectDir, '--out',
                          join(scratch, 'the out'), '--timeout', '9', '--chrome', '/a browser/chrome',
                          '--keep-summary'];
    row(built.runs.length === 14 && built.finds === 1
        && same(last.args, runnerCall(['--out', results, '--timeout', '7', '--chrome', '/a browser/chrome',
                                       '--keep-summary']))
        && artifact.code === 0 && artifact.finds === 1 && artifact.runs.length === 1
        && same(artifact.runs[0].args, wantArtifact)
        && serving.code === 0 && serving.finds === 0 && serving.runs.length === 14
        && same(serving.runs[13].args, [runner, '--serve', ...runnerCall([]).slice(1)]), 'script.runner_arguments',
        `a build: ${last.args.slice(1).join(' ').replaceAll(scratch, '.')}; an artifact: nothing computed, `
        + `${artifact.runs.length} call; --serve: no browser looked for`);

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
    const cases = {
      no_arguments: [sh([]), /a build directory, or --dir/],
      unknown_option: [sh(['--bogus', build]), /unknown option --bogus/],
      missing_value: [sh([build, '--out']), /--out needs a value/],
      empty_value: [sh([build, '--chrome', '']), /--chrome needs a value/],
      bad_timeout: [sh([build, '--timeout', '0']), /--timeout is a whole number/],
      bad_timeout_word: [sh([build, '--timeout', 'soon']), /--timeout is a whole number/],
      two_builds: [sh([build, build]), /one build directory, not two/],
      both_forms: [sh([build, ...dirArgs]), /not both/],
      dir_alone: [sh(['--dir', site]), /--dir needs --live <dir> and --expect <dir>/],
      live_without_dir: [sh(['--live', liveDir, '--expect', expectDir]), /a build directory, or --dir/],
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
    for (const [name, [r, reason]] of Object.entries(cases)) row(refused(r, reason), `usage.${name}`, say(r));
    row(!existsSync(join(scratch, 'never made')) && !existsSync(join(scratch, 'web-live')),
        'usage.nothing_written', 'a run that is refused leaves no results directory');

    const help = sh(['--help']);
    row(help.code === 0 && /Scripts\/web-live\.sh <build-web>/.test(help.out)
        && /^Exit: 0 every row passed/m.test(help.out) && help.err === '', 'usage.help',
        `exit ${help.code}, ${help.out.split('\n').length} lines, the last: ${help.out.trim().split('\n').pop()}`);
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
