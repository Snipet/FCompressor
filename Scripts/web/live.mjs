#!/usr/bin/env node
// Scripts/web/live.mjs: the runner of the browser gate, behind Scripts/web-live.sh (ADR-93, the web lead phase; what
// it holds to is "The gate's contract" in docs/sprints/web-lead.md). On Scripts/web/cdp.mjs: one static server, one
// headless Chrome at a time (always muted), one tab at a time (a hidden tab is neither ticked nor drawn).
//
//   node live.mjs --dir <site> --live <dir> --expect <dir> --out <dir> [--chrome <path>] [--timeout <s>]
//                 [--scenario <file>] [--keep-summary]
//   node live.mjs --serve --dir <site> --live <dir> --expect <dir>
//   node live.mjs --find-chrome [--chrome <path>]
//
// The server gives the site at /, <live> at /live/ and <expect> at /expect/. What is run, in this order:
//   1. the capture pages: the six views x two themes, index.html?view=<id>&theme=<t>&zoom=100&scale=2&dt=<1/60>
//      &nohint=1&nolive=1&host=WEB-LIVE, START never pressed (no AudioContext, no sound). Per page two rows:
//        EQUAL|DIFFERS|FAIL <view>.theme<t>   Module.fcmpFrame() against <expect>/<view>.theme<t>.node.fp, every line
//                                             but `live` and `hooks`; the hooks line must say dpi 2, clock fixed, the
//                                             theme, the pinned dt, drawn 1, idle 1. No expectation is a FAIL.
//        pixels.<view>.theme<t>               Module.fcmpSelftest(): that frame through the WebGL2 sink against
//                                             SoftRaster, judged by the renderer's class (pixelRule below)
//      and a PNG of the canvas.
//   2. frame.chars.sidechain.async: the same page with no dt pin, which is the shipped asynchronous preview, reaches
//      the same frame.
//   3. selftest: the page's own self-test (index.html?selftest=1), its context suspended (no gesture).
//   4. In a second Chrome with --autoplay-policy=no-user-gesture-required (a running AudioContext, still muted):
//      selftest.autoplay, which must have judged page.audio; then page.<name> for every *.html of <live>, by the
//      live-page protocol (document.title RUNNING, then PASS or "FAIL: <row>"; rows in #funkgui-log). A page that
//      gives no verdict within the timeout is a FAIL that names the timeout. No page there is a NOTE.
//   5. scenario: Scripts/web/scenario.mjs --dir <site> --out <out>/scenario --chrome <the browser> as a child
//      process, when the file is there: its rows are copied, and it passes when it ends "scenario: N/N passed" with
//      exit 0. A scenario that is not there is a NOTE; one that does not run by its contract is a FAIL.
// Every line of a page's log and of the scenario is copied, two columns in; the gate's own rows start in column one,
// and only they are counted. The last line is "web-live: N/M passed (results in <out>)".
//
// Results in <out>: frames/<view>.theme<t>.live.fp, png/<view>.theme<t>.png, png/<page>.png, selftest.log,
// selftest.autoplay.log, <page>.log, scenario.log, scenario/, summary.txt. An earlier run's are removed before a run
// (nothing else in <out> is); <expect> is only read.
//
//   --timeout <s>     how long one page may take to give its verdict (120); the scenario may take five times that
//   --keep-summary    summary.txt is added to, not replaced (web-live.sh has written its head)
//   --scenario <f>    another scenario file (a test's)
//   --serve           no browser: serves and prints the URLs, with what each capture page must give, until
//                     interrupted. The port is a random free one, so the origin is fresh: no stale localStorage.
//   --find-chrome     prints the Chrome the gate would start, and nothing else
//   FCMP_WEB_LIVE_NO_SANDBOX=1   Chrome without its sandbox, for a container that gives it no user namespace (read by
//                     cdp.mjs, so the scenario's Chrome takes it too)
// Chrome's flags are fixed (cdp.mjs): there is no pass-through.
//
// Exit: 0 every row passed; 1 a view differs or a row failed; 2 usage, no Chrome, or no verdict (Chrome went away,
// the runner failed, nothing was judged).
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync, realpathSync, rmSync, statSync, writeFileSync,
         appendFileSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chrome, chromePlaces, cleanUp, findChrome, gpuFlags, HERE, serve, sleep } from './cdp.mjs';

export const VIEWS = ['panel', 'chars.sidechain', 'chars.colour', 'modebrowser', 'presetbrowser', 'settings'];
export const THEMES = [0, 1];
export const DT = '0.0166666675';                       // 1/60 s as a float, written %.9g: the headless dt
export const HOST = 'WEB-LIVE';
export const ASYNC_VIEW = 'chars.sidechain';            // the view with the computed preview
const TEST = 'web.live';
const WINDOW = { width: 1280, height: 1000 };           // room for the 960 x 640 editor at zoom 100
const USAGE = 'usage: live.mjs --dir <site> --live <dir> --expect <dir> --out <dir> [--chrome <path>] '
            + '[--timeout <s>] [--scenario <file>] [--keep-summary]\n'
            + '       live.mjs --serve --dir <site> --live <dir> --expect <dir>\n'
            + '       live.mjs --find-chrome [--chrome <path>]';

// ---- what a run is told ---------------------------------------------------------------------------------------------
// The arguments as { opt } or { error }.
export function parseArgs(argv) {
  const opt = { dir: '', live: '', expect: '', out: '', chrome: '', timeoutS: 120, scenario: join(HERE, 'scenario.mjs'),
                serve: false, findChrome: false, keepSummary: false };
  const takes = { '--dir': 'dir', '--live': 'live', '--expect': 'expect', '--out': 'out', '--chrome': 'chrome',
                  '--scenario': 'scenario' };
  const flags = { '--serve': 'serve', '--find-chrome': 'findChrome', '--keep-summary': 'keepSummary' };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    if (a in takes || a === '--timeout') {
      const value = argv[i += 1];
      if (value === undefined || value === '') return { error: `${a} needs a value` };
      if (a === '--timeout') opt.timeoutS = Number(value);
      else opt[takes[a]] = value;
    } else if (a in flags) {
      opt[flags[a]] = true;
    } else {
      return { error: `unknown argument ${a}` };
    }
  }
  if (!(opt.timeoutS > 0 && Number.isFinite(opt.timeoutS))) {
    return { error: '--timeout is a number of seconds above 0' };
  }
  if (opt.findChrome) return { opt };
  for (const need of ['dir', 'live', 'expect']) if (opt[need] === '') return { error: `--${need} is required` };
  if (!opt.serve && opt.out === '') return { error: '--out is required' };
  return { opt };
}

// A capture page's address under `base` (no slash at its end). dt '' leaves the clock free: the shipped preview.
export function captureUrl(base, view, theme, dt = DT) {
  return `${base}/index.html?view=${view}&theme=${theme}&zoom=100&scale=2${dt === '' ? '' : `&dt=${dt}`}`
       + `&nohint=1&nolive=1&host=${HOST}`;
}

// ---- the frame ------------------------------------------------------------------------------------------------------
// The lines of Module.fcmpFrame() or of a .node.fp file, "name value", as a Map in their order.
export function parseFrame(text) {
  const rows = new Map();
  for (const line of String(text).split('\n')) {
    if (line.trim() === '') continue;
    const at = line.indexOf(' ');
    const name = at < 0 ? line : line.slice(0, at);
    rows.set(rows.has(name) ? `${name} (again)` : name, at < 0 ? '' : line.slice(at + 1));
  }
  return rows;
}

// What is wrong with a hooks line (the text after "hooks "), as a list; empty when the capture hooks reached the
// editor and the frame is a settled, drawn one. `want`: { theme, clock ('fixed' or 'free') }.
export function judgeHooks(line, want) {
  if (line === undefined) return ['the frame has no hooks line'];
  const m = /^dpi (\S+) clock (\S+) theme (\S+) dt (\S+) settle (\d+) drawn ([01]) idle ([01])$/.exec(line);
  if (!m) return [`the hooks line is not the contract's: '${line}'`];
  const [, dpi, clock, theme, dt, settle, drawn, idle] = m;
  const wrong = [];
  if (dpi !== '2') wrong.push(`dpi ${dpi}, not 2 (the pins scale=2 and zoom=100 did not reach the editor)`);
  if (clock !== want.clock) wrong.push(`clock ${clock}, not ${want.clock}`);
  if (theme !== String(want.theme)) wrong.push(`theme ${theme}, not ${want.theme}`);
  if (want.clock === 'fixed' && dt !== DT) wrong.push(`dt ${dt}, not ${DT}`);
  if (drawn !== '1') wrong.push('the frame was not drawn (a hidden tab, or no WebGL context)');
  else if (idle !== '1') wrong.push(`the Panel had not settled after ${settle} frames`);
  return wrong;
}

// The browser's frame against the node value. `expectText` null: there is no expectation, which is a FAIL and never a
// pass. Answers { status: 'EQUAL' | 'DIFFERS' | 'FAIL', why (of a FAIL), differ (one text per differing line),
// compared, hooks, live { geometry, text }, node { geometry, text } }.
const NOT_COMPARED = new Set(['live', 'hooks']);
export function judgeFrame(liveText, expectText, want) {
  const live = parseFrame(liveText ?? '');
  const pair = (rows) => ({ geometry: rows.get('geometry') ?? '?', text: rows.get('text') ?? '?' });
  const out = { status: 'FAIL', why: '', differ: [], compared: 0, hooks: live.get('hooks') ?? '', live: pair(live),
                node: { geometry: '?', text: '?' } };
  if (expectText === null || expectText === undefined) {
    out.why = 'no expectation';
    return out;
  }
  const expect = parseFrame(expectText);
  out.node = pair(expect);
  if (!expect.has('geometry') || !expect.has('text')) {
    out.why = 'the expectation has no geometry or no text line';
    return out;
  }
  if (live.size === 0) {
    out.why = 'Module.fcmpFrame() gave nothing (the module has shut down)';
    return out;
  }
  for (const [name, value] of expect) {
    if (NOT_COMPARED.has(name)) continue;
    out.compared += 1;
    if (!live.has(name)) out.differ.push(`${name}: not in the browser's frame (node ${value})`);
    else if (live.get(name) !== value) out.differ.push(`${name}: live ${live.get(name)}, node ${value}`);
  }
  for (const [name, value] of live) {
    if (!NOT_COMPARED.has(name) && !expect.has(name)) out.differ.push(`${name}: live ${value}, not in the node value`);
  }
  const wrong = judgeHooks(live.get('hooks'), want);
  if (wrong.length > 0) out.why = wrong.join('; ');
  else out.status = out.differ.length === 0 ? 'EQUAL' : 'DIFFERS';
  return out;
}

// ---- the pixels -----------------------------------------------------------------------------------------------------
// A WebGL renderer's class by its name: 'software' (SwiftShader, llvmpipe, softpipe, a software rasteriser) or 'gpu',
// which an unknown name is too. The same rule as the page's own editor.pixels row.
export function rendererClass(name) {
  return /swiftshader|llvmpipe|softpipe|software|basic render/i.test(String(name)) ? 'software' : 'gpu';
}
// Module.fcmpSelftest()'s pixels { frames, largest, over2, samples } judged: a GPU has no sample over 2 of 255; a
// software renderer gets FunkGui's bound, none over 16 and at most 10 per mille over 2. Answers { ok, detail }.
export function pixelRule(pixels, renderer) {
  const p = pixels || {};
  const software = rendererClass(renderer) === 'software';
  const read = p.frames >= 1 && p.samples > 0;
  const ok = read && (software ? p.largest <= 16 && p.over2 * 1000 <= 10 * p.samples : p.over2 === 0);
  return { ok, detail: `${p.frames} frame(s) through the sink, ${p.samples} samples against SoftRaster, the largest `
                     + `difference ${p.largest} of 255, ${p.over2} over 2 (${software
                       ? 'a software renderer: none over 16, at most 10 per mille over 2' : 'a GPU: none over 2'})` };
}

// ---- a page by the self-test's protocol -----------------------------------------------------------------------------
export const isVerdict = (title) => title === 'PASS' || String(title).startsWith('FAIL');
// A page's end judged: `title` and `log` as last read, `uncaught` what the runner itself saw thrown in the page.
// A PASS needs the title PASS, at least one PASS row, no FAIL line and nothing uncaught. Answers { ok, detail }.
export function judgePage({ title, log, uncaught = [], timeoutS }) {
  const lines = String(log).split('\n');
  const rows = (word) => lines.filter((l) => new RegExp(`^${word} +[^ :]+ +\\S`).test(l)).length;   // not a last line
  const passed = rows('PASS');
  const failed = lines.filter((l) => /^FAIL\b/.test(l));
  if (!isVerdict(title)) {
    return { ok: false, detail: uncaught.length > 0
      ? `no verdict after an uncaught error: ${uncaught[0]} (the title is '${title}')`
      : `no verdict within ${timeoutS} s (the title is '${title}')` };
  }
  if (title !== 'PASS') return { ok: false, detail: `${title} (${passed} row(s) passed, ${rows('FAIL')} failed)` };
  if (failed.length > 0) {
    return { ok: false, detail: `the title says PASS, but the log has ${failed.length} FAIL line(s): ${failed[0]}` };
  }
  if (uncaught.length > 0) return { ok: false, detail: `the title says PASS after an uncaught error: ${uncaught[0]}` };
  if (passed === 0) return { ok: false, detail: 'the title says PASS, but the log has no PASS row' };
  return { ok: true, detail: `PASS, ${passed} row(s)` };
}

// ---- the scenario ---------------------------------------------------------------------------------------------------
// The scenario's end judged by its contract: PASS|FAIL|NOTE rows, a last line "scenario: N/M passed", exit 0 (all
// passed), 1 (a row failed) or 2 (it could not run). `output` is what it printed; `code` null when it was killed.
export function judgeScenario({ code, output, timedOutS = 0 }) {
  const lines = String(output).split('\n').map((l) => l.trimEnd()).filter((l) => l !== '');
  const last = lines[lines.length - 1] || '';
  const m = /^scenario: (\d+)\/(\d+) passed\b/.exec(last);
  if (timedOutS > 0) {
    return { ok: false, detail: `no end within ${timedOutS} s (its last line: '${last.slice(0, 200)}')` };
  }
  if (!m) {
    const error = lines.find((l) => /(^|\s)(\w*Error\b|DRIVER ERROR\b)/.test(l) && l !== last);
    return { ok: false, detail: `it did not run by the contract: exit ${code}, and its last line is not `
                              + `'scenario: N/M passed' but '${last.slice(0, 200)}'`
                              + `${error ? ` (it said: ${error.trim().slice(0, 300)})` : ''}` };
  }
  const [n, total] = [Number(m[1]), Number(m[2])];
  const ok = code === 0 && n === total && total > 0;
  return { ok, detail: `${n}/${total} passed, exit ${code}${ok || (code === 0) === (n === total) ? ''
                       : ' (the exit code and the count disagree)'}` };
}

// ---- the rows of a run ----------------------------------------------------------------------------------------------
// What the gate prints and keeps, and its count: only row() counts; say() copies a line.
export class Tally {
  constructor(print = console.log) {
    this.print = print;
    this.lines = [];
    this.total = 0;
    this.passed = 0;
  }

  say(line) {
    this.print(line);
    this.lines.push(line);
  }

  // A frame row: status is EQUAL, DIFFERS or FAIL.
  frame(status, name, detail) {
    this.total += 1;
    if (status === 'EQUAL') this.passed += 1;
    this.say(`${status.padEnd(8)} ${name}: ${detail}`);
  }

  row(ok, name, detail) {
    this.total += 1;
    if (ok) this.passed += 1;
    this.say(`${ok ? 'PASS' : 'FAIL'}     ${TEST} ${name}: ${detail}`);
    return ok;
  }

  note(text) {
    this.say(`NOTE     ${TEST} ${text}`);
  }

  last(out) {
    return `web-live: ${this.passed}/${this.total} passed (results in ${out})`;
  }

  // 0 every row passed, 1 one did not, 2 nothing was judged (no verdict).
  exitCode() {
    return this.total === 0 ? 2 : this.passed === this.total ? 0 : 1;
  }
}

// ---- the run --------------------------------------------------------------------------------------------------------
class NoVerdict extends Error {}                        // exit 2: said as it is
const isDir = (path) => existsSync(path) && statSync(path).isDirectory();
const readOr = (file, otherwise) => (existsSync(file) ? readFileSync(file, 'utf8') : otherwise);
const indent = (text) => String(text).trimEnd().split('\n').filter((l) => l !== '').map((l) => `  ${l}`);
const firstLine = (text) => String(text).split('\n')[0].trim();
// `promise`'s value, or undefined when it has none after `ms`. The promise must not reject.
const within = (ms, promise) => {
  let timer = 0;
  return Promise.race([promise, new Promise((r) => { timer = setTimeout(r, ms); })]).finally(() => clearTimeout(timer));
};

function checkDirs(opt) {
  if (!existsSync(join(opt.dir, 'index.html'))) throw new NoVerdict(`${opt.dir} is not a site (no index.html)`);
  if (!isDir(opt.live)) throw new NoVerdict(`no live directory ${opt.live}`);
  if (!isDir(opt.expect)) throw new NoVerdict(`no expectation directory ${opt.expect}`);
}
const livePages = (dir) => readdirSync(dir).filter((f) => f.endsWith('.html')).sort();

async function serveOnly(opt) {
  checkDirs(opt);
  const server = await serve(opt.dir, { live: opt.live, expect: opt.expect });
  const say = console.log;
  say(`web-live: serving at ${server.base}/ until interrupted (Ctrl-C)`);
  say(`  /         ${opt.dir}`);
  say(`  /live/    ${opt.live}`);
  say(`  /expect/  ${opt.expect}`);
  say(`the page:       ${server.base}/`);
  say(`the self-test:  ${server.base}/index.html?selftest=1  (the title becomes PASS or FAIL: <row>)`);
  say('the capture pages: in the console, Module.fcmpFrame() must give these geometry and text lines');
  for (const view of VIEWS) {
    const want = parseFrame(readOr(join(opt.expect, `${view}.theme0.node.fp`), ''));
    say(`  ${captureUrl(server.base, view, 0)}`);
    say(want.has('geometry') ? `      geometry ${want.get('geometry')} text ${want.get('text')}`
                             : `      no expectation (${join(opt.expect, `${view}.theme0.node.fp`)})`);
  }
  const pages = livePages(opt.live);
  say(pages.length > 0 ? 'the live pages (the title becomes PASS or FAIL: <row>):' : 'no live page');
  for (const page of pages) say(`  ${server.base}/live/${page}`);
  say('The port is a free one chosen now: a fresh origin, with no settings stored from an earlier visit.');
  return new Promise(() => {});                         // the server keeps the process; a signal ends it (cdp.mjs)
}

// The gate: its exit code. `opt` as parseArgs gives it, the paths absolute; opt.gpu (cdp.mjs's gpuFlags) is for a
// run of the gate on another renderer than the platform's.
export async function gate(opt) {
  checkDirs(opt);
  const exe = findChrome(opt.chrome);
  if (exe === '') throw new NoVerdict(`no Chrome (${chromePlaces(opt.chrome)})`);
  const { out } = opt;

  // The results of an earlier run go: <out> is one when it has frames/. In any other directory nothing is removed
  // (--out may name a directory that holds something else). The expectations are only read, wherever they are.
  mkdirSync(out, { recursive: true });
  const earlier = existsSync(join(out, 'frames'));
  for (const name of readdirSync(out)) {
    const result = name === 'frames' || name === 'png' || name === 'scenario' || name.endsWith('.log');
    if ((earlier && result) || (name === 'summary.txt' && !opt.keepSummary)) {
      rmSync(join(out, name), { recursive: true, force: true });
    }
  }
  mkdirSync(join(out, 'frames'), { recursive: true });
  mkdirSync(join(out, 'png'), { recursive: true });

  // Every line goes to summary.txt as it is said: a run that is interrupted has left what it found.
  const summary = join(out, 'summary.txt');
  if (!opt.keepSummary) writeFileSync(summary, '');
  const tally = new Tally((line) => {
    console.log(line);
    appendFileSync(summary, `${line}\n`);
  });
  try {
    await rows(opt, exe, tally);
    tally.print(tally.last(out));
    return tally.exitCode();
  } catch (e) {
    tally.print(`web-live: no verdict (${e instanceof NoVerdict ? e.message : (e && e.stack) || e})`);
    return 2;
  }
}

async function rows(opt, exe, tally) {
  const { out } = opt;
  const timeoutMs = opt.timeoutS * 1000;
  const started = Date.now();
  const server = await serve(opt.dir, { live: opt.live, expect: opt.expect });
  const built = readOr(join(opt.dir, 'built-from.txt'), 'no built-from.txt').trim();
  tally.say(`web-live: ${opt.dir} (${built}) at ${server.base}/, live ${opt.live}, expect ${opt.expect}`);

  const launch = async (autoplay) => {
    try {
      return await chrome({ ...WINDOW, chrome: exe, autoplay, gpu: gpuFlags(opt.gpu), flags: [],
                            png: join(out, 'png') });
    } catch (e) {
      throw new NoVerdict(`${e.message}${/sandbox/i.test(e.message) && process.env.FCMP_WEB_LIVE_NO_SANDBOX !== '1'
        ? ' (where Chrome is given no user namespace, set FCMP_WEB_LIVE_NO_SANDBOX=1)' : ''}`);
    }
  };
  // A tab on `url`. `sharp`: a device pixel ratio of 2, for a capture page's picture only (the pins scale=2 and
  // zoom=100 set the editor's scale: the override does not).
  const open = async (browser, url, sharp = false) => {
    const p = await browser.page(null);
    if (sharp) await p.metrics(WINDOW.width, WINDOW.height, 2);
    const refused = await p.go(url, 0);
    if (refused !== '') {
      await p.close();
      throw new Error(`${url}: ${refused}`);
    }
    return p;
  };
  // A row's work: Chrome going away is the run's end (no verdict); anything else thrown is that row's failure.
  const guarded = async (browser, work, failed) => {
    try {
      await work();
    } catch (e) {
      if (browser.gone() !== '') throw new NoVerdict(browser.gone());
      failed(`the runner could not go on (${e.message})`);
    }
  };

  // ---- 1. the capture pages, 2. the asynchronous preview, 3. the self-test with no gesture -------------------------
  let browser = await launch(false);
  const renderer = await browser.renderer().catch(() => '');
  tally.note(`browser: ${browser.version.product}, ${exe}`);
  tally.note(`renderer: ${renderer || 'no WebGL2'} (judged as ${rendererClass(renderer) === 'software'
             ? 'a software renderer' : 'a GPU'})`);
  // The editor as the page has it: 'ready', 'old' (a module without the frame export), 'lost: <what the page says>',
  // or '' while it loads.
  const EDITOR = `(() => {
    const m = globalThis.Module;
    if (m && typeof m.fcmpFrame === 'function') return 'ready';
    if (m && typeof m.fcmpStatus === 'function') return 'old';
    const page = globalThis.fcmpPage;
    if (page && /^(failed|refused|stopped)$/.test(page.state())) {
      return 'lost: ' + document.getElementById('fcmp-status').textContent;
    }
    return '';
  })()`;
  const editorUp = async (p) => {
    const state = await p.until(EDITOR, timeoutMs);
    if (state === 'ready') return '';
    if (state === 'old') return 'the module has no Module.fcmpFrame: it is not the module this gate is written for';
    const thrown = p.exceptions.length > 0 ? ` (${firstLine(p.exceptions[0])})` : '';
    if (state === null) return `the editor did not come up in ${opt.timeoutS} s${thrown}`;
    return `the page says "${state.slice(6)}"${thrown}`;
  };
  let hostSaid = false;
  for (const view of VIEWS) {
    for (const theme of THEMES) {
      const name = `${view}.theme${theme}`;
      const expectFile = join(opt.expect, `${name}.node.fp`);
      const t0 = Date.now();
      let framed = false;
      let pixelled = false;
      const failRest = (why) => {
        if (!framed) tally.frame('FAIL', name, why);
        if (!pixelled) tally.row(false, `pixels.${name}`, `not read: ${why}`);
        framed = pixelled = true;
      };
      await guarded(browser, async () => {
        const p = await open(browser, captureUrl(server.base, view, theme), true);
        try {
          const down = await editorUp(p);
          if (down !== '') { failRest(down); return; }
          const frame = await p.ev('Module.fcmpFrame()');
          writeFileSync(join(out, 'frames', `${name}.live.fp`), frame);
          const self = JSON.parse(await p.ev('Module.fcmpSelftest()'));
          const status = JSON.parse(await p.ev('Module.fcmpStatus()'));
          // The editor alone in the picture: the page's overlay and its `waiting` veil are taken away.
          await p.ev(`(async () => {
            document.getElementById('fcmp-overlay').style.display = 'none';
            document.getElementById('fcmp-stage').classList.remove('waiting');
            await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
          })()`);
          const png = await p.shot(join(out, 'png', `${name}.png`));
          const v = judgeFrame(frame, readOr(expectFile, null), { theme, clock: 'fixed' });
          const ms = Date.now() - t0;
          framed = true;
          if (p.exceptions.length > 0) {
            tally.frame('FAIL', name, `an uncaught error in the page: ${firstLine(p.exceptions[0])}`);
          } else if (v.status === 'EQUAL') {
            tally.frame('EQUAL', name, `geometry ${v.live.geometry} text ${v.live.text} (${v.compared} lines; `
                                       + `${v.hooks}; ${ms} ms)`);
          } else if (v.status === 'DIFFERS') {
            tally.frame('DIFFERS', name, `live geometry ${v.live.geometry} text ${v.live.text}, node geometry `
                        + `${v.node.geometry} text ${v.node.text} (${v.differ.length} of ${v.compared} lines differ; `
                        + `${v.hooks}; ${ms} ms); see ${png}`);
          } else {
            tally.frame('FAIL', name, v.why === 'no expectation'
              ? `no expectation ${expectFile}${existsSync(expectFile.replace(/\.fp$/, '.log'))
                ? ` (the node side failed: see ${expectFile.replace(/\.fp$/, '.log')})` : ''}`
              : `${v.why} (hooks: ${v.hooks || 'none'}; ${ms} ms)`);
          }
          if (v.status !== 'EQUAL' && v.why !== 'no expectation') {
            for (const d of v.differ.slice(0, 20)) tally.say(`         ${d}`);
            if (v.differ.length > 20) tally.say(`         ... and ${v.differ.length - 20} more`);
            if (v.differ.length > 0 && v.live.geometry === v.node.geometry) {
              tally.say('         the geometry is equal: what differs is a text');
            }
            if (v.differ.length > 0 && status.host !== HOST) {
              tally.say(`         the module's host is '${status.host}', not the pinned '${HOST}': it did not take `
                        + 'the host pin');
            }
          }
          const px = pixelRule(self.pixels, renderer);
          pixelled = true;
          tally.row(px.ok, `pixels.${name}`, px.detail);
          if (!hostSaid && status.host !== HOST) {
            hostSaid = true;
            tally.note(`host: the module says '${status.host}' under the pin host=${HOST}: this module does not read `
                       + 'the host pin, so its settings view names the browser');
          }
        } finally {
          await p.close();
        }
      }, failRest);
    }
  }

  {
    const name = `frame.${ASYNC_VIEW}.async`;
    const expectFile = join(opt.expect, `${ASYNC_VIEW}.theme0.node.fp`);
    let done = false;
    const fail = (why) => { if (!done) tally.row(false, name, why); done = true; };
    await guarded(browser, async () => {
      const p = await open(browser, captureUrl(server.base, ASYNC_VIEW, 0, ''));
      try {
        const down = await editorUp(p);
        if (down !== '') { fail(down); return; }
        // The preview is computed once the view rests, in a later task: the frame is asked again until it is the
        // node value, for at most the page's timeout.
        const t0 = Date.now();
        let v = null;
        let asked = 0;
        do {
          if (asked > 0) await sleep(100);
          const frame = await p.ev('Module.fcmpFrame()');
          asked += 1;
          writeFileSync(join(out, 'frames', `${ASYNC_VIEW}.async.live.fp`), frame);
          v = judgeFrame(frame, readOr(expectFile, null), { theme: 0, clock: 'free' });
        } while (v.status !== 'EQUAL' && v.why !== 'no expectation' && Date.now() - t0 < timeoutMs);
        done = true;
        const waited = `${Date.now() - t0} ms, ${asked} frame(s) asked`;
        if (v.status === 'EQUAL') {
          tally.row(true, name, `with no dt pin (the shipped preview, computed after the view rests) the frame is the `
                    + `node value: geometry ${v.live.geometry} text ${v.live.text} (${v.hooks}; ${waited})`);
        } else {
          tally.row(false, name, v.why === 'no expectation' ? `no expectation ${expectFile}`
            : `with no dt pin the frame is not the node value after ${waited}: `
              + `${v.why || `${v.differ.length} of ${v.compared} lines differ`} (${v.hooks})`);
          for (const d of v.differ.slice(0, 20)) tally.say(`         ${d}`);
        }
      } finally {
        await p.close();
      }
    }, fail);
  }

  // A page that speaks the self-test's protocol: its verdict, its log copied, a picture, one row.
  const runPage = async (name, url, logFile, also = () => '') => {
    let done = false;
    const fail = (why) => { if (!done) tally.row(false, name, why); done = true; };
    await guarded(browser, async () => {
      const t0 = Date.now();
      const p = await open(browser, url);
      try {
        // Until the verdict, for at most the timeout; a page that threw and says nothing of it gets 2 s more. A page
        // whose main thread never rests answers nothing: each question waits for the rest of the timeout at most.
        const left = () => Math.max(timeoutMs - (Date.now() - t0), 1000);
        let thrownAt = 0;
        for (;;) {
          if (isVerdict(await within(left(), p.ev('document.title').catch(() => '')))) break;
          if (browser.gone() !== '') throw new Error(browser.gone());
          if (thrownAt === 0 && p.exceptions.length > 0) thrownAt = Date.now();
          if (Date.now() - t0 >= timeoutMs || (thrownAt > 0 && Date.now() - thrownAt >= 2000)) break;
          await sleep(100);
        }
        const read = await within(5000, p.ev(
          "[document.title, (document.getElementById('funkgui-log') || {}).textContent || '']").catch(() => undefined));
        if (browser.gone() !== '') throw new Error(browser.gone());
        if (read === undefined) {
          fail(`no verdict within ${opt.timeoutS} s: the page no longer answers (its main thread never rests, or it `
               + `crashed) (${Date.now() - t0} ms)`);
          return;
        }
        const [title, log] = read;
        writeFileSync(join(out, logFile), log);
        await within(10000, p.shot(join(out, 'png', logFile.replace(/\.log$/, '.png')), { canvasOnly: false })
          .catch(() => {}));
        for (const line of indent(log)) tally.say(line);
        const uncaught = p.exceptions.map(firstLine);
        for (const line of uncaught) tally.say(`  uncaught: ${line}`);
        const judged = judgePage({ title, log, uncaught, timeoutS: opt.timeoutS });
        const more = judged.ok ? also(log) : '';
        done = true;
        tally.row(judged.ok && more === '', name, `${more !== '' ? more : judged.detail} (${Date.now() - t0} ms)`);
      } finally {
        await p.close();
      }
    }, fail);
  };
  const contextOf = (log) => (/the context is (\w+)/.exec(log) || [])[1] || 'unknown';
  await runPage('selftest', `${server.base}/index.html?selftest=1`, 'selftest.log');
  tally.note(`selftest: with no gesture the page's context was ${contextOf(readOr(join(out, 'selftest.log'), ''))}`);
  await browser.close();

  // ---- 4. with a running context: the self-test again, then the live pages ---------------------------------------
  browser = await launch(true);
  await runPage('selftest.autoplay', `${server.base}/index.html?selftest=1`, 'selftest.autoplay.log',
                (log) => (/^PASS +web\.selftest page\.audio:/m.test(log) ? ''
                  : 'the page did not judge page.audio: under the autoplay flag its context must run'));
  const pages = livePages(opt.live);
  if (pages.length === 0) tally.note(`pages: ${opt.live} has no *.html page`);
  for (const page of pages) {
    const stem = basename(page, '.html');
    await runPage(`page.${stem}`, `${server.base}/live/${page}`, `${stem}.log`);
  }
  await browser.close();
  const missed = new Map();
  for (const miss of server.misses) missed.set(miss, (missed.get(miss) || 0) + 1);
  for (const [miss, n] of missed) tally.note(`server: ${miss}${n > 1 ? ` (${n} times)` : ''}`);
  server.kill();

  // ---- 5. the scripted user ---------------------------------------------------------------------------------------
  if (!existsSync(opt.scenario)) {
    tally.note(`scenario: no ${opt.scenario}: the scripted user is not run`);
  } else {
    const t0 = Date.now();
    const boundS = opt.timeoutS * 5;
    const args = [opt.scenario, '--dir', opt.dir, '--out', join(out, 'scenario'), '--chrome', exe];
    // Its own process group: at the bound the scenario and the Chrome it started go together.
    const child = spawn(process.execPath, args, { stdio: ['ignore', 'pipe', 'pipe'], detached: true });
    const killGroup = () => {                           // only while the child lives: its number is its own then
      if (child.exitCode !== null || child.signalCode !== null) return;
      try { process.kill(-child.pid, 'SIGKILL'); } catch { /* gone */ }
    };
    process.on('exit', killGroup);
    let output = '';
    let held = '';
    const hear = (chunk) => {
      output += chunk;
      held += chunk;
      const lines = held.split('\n');
      held = lines.pop();
      for (const line of lines) if (line.trim() !== '') tally.say(`  ${line.trimEnd()}`);
    };
    child.stdout.on('data', hear);
    child.stderr.on('data', hear);
    let timer = 0;
    const end = await new Promise((ended) => {
      timer = setTimeout(() => {
        child.kill('SIGTERM');                          // cdp.mjs in the scenario stops its Chrome on this
        setTimeout(killGroup, 3000);
        ended({ code: null, late: true });
      }, boundS * 1000);
      child.on('error', (e) => { output += `could not start: ${e.message}\n`; ended({ code: null, late: false }); });
      child.on('close', (code, signal) => ended({ code: code ?? signal, late: false }));
    });
    clearTimeout(timer);
    if (held.trim() !== '') tally.say(`  ${held.trimEnd()}`);
    writeFileSync(join(out, 'scenario.log'), output);
    const judged = judgeScenario({ code: end.code, output, timedOutS: end.late ? boundS : 0 });
    tally.row(judged.ok, 'scenario', `${judged.detail} (${Date.now() - t0} ms; the whole of it in `
                                     + `${join(out, 'scenario.log')})`);
  }

  tally.note(`time: ${((Date.now() - started) / 1000).toFixed(1)} s`);
}

async function main() {
  const parsed = parseArgs(process.argv.slice(2));
  if (parsed.error) {
    console.error(`web-live: ${parsed.error}\n${USAGE}`);
    return 2;
  }
  const { opt } = parsed;
  if (opt.findChrome) {
    const exe = findChrome(opt.chrome);
    if (exe === '') {
      console.error(`web-live: no Chrome (${chromePlaces(opt.chrome)})`);
      return 2;
    }
    console.log(exe);
    return 0;
  }
  for (const key of ['dir', 'live', 'expect', 'out', 'scenario']) if (opt[key] !== '') opt[key] = resolve(opt[key]);
  try {
    return await (opt.serve ? serveOnly(opt) : gate(opt));
  } catch (e) {
    console.error(`web-live: ${e instanceof NoVerdict ? e.message : (e && e.stack) || e}`);
    return 2;
  }
}

// Run as a program, not imported (web/tests/weblive.mjs imports the pieces above).
if (process.argv[1] && realpathSync(process.argv[1]) === realpathSync(fileURLToPath(import.meta.url))) {
  main().then((code) => {
    cleanUp();
    process.exit(code);
  });
}
