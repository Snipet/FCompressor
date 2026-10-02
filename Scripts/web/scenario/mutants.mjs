#!/usr/bin/env node
// Scripts/web/scenario/mutants.mjs: the proof that the scripted user's rows can fail. Each mutant is a copy of the
// site with one thing broken, by one text put into one shipped file; the scenario (Scripts/web/scenario.mjs) runs on
// it, and the rows that guard the broken thing must come out FAIL. Not part of the gate and not a group of the
// scenario: run by hand when a row or the page changes.
//
//   node Scripts/web/scenario/mutants.mjs --dir <site> --out <dir> [--only <mutant>[,<mutant>...]] [--list] [--keep]
//                                         [--chrome <path>] [--chrome-flag <switch>]...
//
//   --dir <site>     the site as it is built (build-web/site): never written to
//   --out <dir>      <dir>/<mutant>.log is the scenario's output on that mutant; <dir>/<mutant>/ is the mutated site,
//                    removed afterwards unless --keep
//   --only, --list   these mutants only; the mutants' names and what each breaks
//   --chrome, --chrome-flag   passed to the scenario
//
// A mutant passes when the scenario ends with exit 1 on it and every row the mutant names is red. A text that is to
// be replaced must be in its file exactly once: when the page has changed so that it is not, the run ends with exit 2
// and the mutant must be written again. Output: PASS|FAIL rows, then `mutants: N/M passed`. Exit 0, 1, or 2 (usage,
// no site, a mutant that does not apply).
import { spawn } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

// At the top of the editor's script, before the module has a listener of its own: one of the window's, in the
// capture phase, hears an event first and can keep it from every other.
const deaf = (type, when) => `window.addEventListener('${type}', (e) => { if (${when}) e.stopImmediatePropagation(); `
                             + '}, true);\n';
const ON_CANVAS = "e.target && e.target.id === 'fcmp-canvas'";

// { name, what, edits: [{ file, find ('' puts the text at the top of the file), put }], groups (the scenario's, run
// with --only), red (rows that must fail) }.
const MUTANTS = [
  { name: 'drop-params',
    what: 'the worklet drops every Params record after its third record: an edit never reaches the engine',
    edits: [{ file: 'fcmp-worklet.js', find: '    this.records += 1;\n',
              put: '    this.records += 1;\n    if (buffer.byteLength === 140 && this.records > 3) return;\n' }],
    groups: ['start', 'values', 'edits', 'presets', 'modes', 'quality', 'context', 'hidden'],
    red: ['values.drag', 'values.double-click', 'values.wheel', 'values.typed', 'values.typed.unit', 'values.keys',
          'edits.undo.chord', 'edits.redo.chord', 'edits.undo.buttons', 'edits.ab.switch', 'edits.ab.menu.keys',
          'edits.ab.menu.click', 'presets.browser.click', 'presets.browser.double-click', 'presets.browser.keys',
          'presets.next-previous', 'presets.wrap', 'presets.modified', 'modes.arrows', 'modes.wheel', 'modes.stepped',
          'modes.continuous', 'quality.hq', 'quality.5ms', 'quality.20ms', 'quality.eco', 'quality.std',
          'quality.settings', 'context.lost.edit', 'hidden.edit'] },
  { name: 'no-hook',
    what: 'the module gives no Module.fcmpA11y(): the Panel has no list to find a control in',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: 'globalThis.Module = globalThis.Module || {};\n'
                   + "Object.defineProperty(globalThis.Module, 'fcmpA11y', "
                   + '{ get: () => undefined, set: () => {} });\n' }],
    groups: ['start', 'screens', 'values', 'edits', 'presets', 'modes', 'quality', 'context', 'hidden', 'zoom',
             'pictures'],
    red: ['start.list', 'screens.driver', 'values.driver', 'edits.driver', 'presets.driver', 'modes.driver',
          'quality.driver', 'context.driver', 'hidden.driver', 'zoom.driver', 'pictures.driver'] },
  { name: 'dead-start',
    what: 'a press on START does nothing',
    edits: [{ file: 'main.js', find: "if (state === 'idle') start().catch(fail);",
              put: "if (state === 'idle') return;" }],
    groups: ['start', 'screens', 'values', 'edits', 'presets', 'modes', 'quality', 'file', 'context', 'hidden', 'zoom',
             'pictures'],
    red: ['start.running', 'screens.driver', 'values.driver', 'edits.driver', 'presets.driver', 'modes.driver',
          'quality.driver', 'file.driver', 'context.start', 'hidden.driver', 'zoom.start', 'pictures.driver',
          'errors.records'] },
  { name: 'dead-canvas',
    what: 'no press reaches the editor: the canvas hears no pointer',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: deaf('pointerdown', ON_CANVAS) + deaf('mousedown', ON_CANVAS) }],
    groups: ['screens', 'values', 'presets', 'modes', 'zoom'],
    red: ['screens.characteristics', 'screens.driver', 'values.drag', 'values.double-click', 'values.typed',
          'presets.driver', 'modes.arrows', 'zoom.steps', 'zoom.reload'] },
  { name: 'dead-corner',
    what: 'no press reaches the lower right of the editor: the tabs of CHARACTERISTICS, the zoom cells, the themes',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: deaf('pointerdown', `${ON_CANVAS} && e.offsetX > 0.72 * e.target.clientWidth `
                                       + '&& e.offsetY > 0.6 * e.target.clientHeight') }],
    groups: ['screens', 'zoom'],
    red: ['screens.characteristics.colour', 'zoom.steps', 'zoom.reload'] },
  { name: 'deaf-escape',
    what: 'the Escape key reaches nothing',
    edits: [{ file: 'fcmp-ui.js', find: '', put: deaf('keydown', "e.key === 'Escape'") }],
    groups: ['screens', 'values', 'edits'],
    red: ['screens.characteristics.closes', 'screens.settings', 'screens.modebrowser', 'values.typed.refused',
          'values.typed.cancelled', 'edits.ab.menu'] },
  { name: 'deaf-wheel',
    what: 'the wheel never reaches the editor (the page scrolls instead)',
    edits: [{ file: 'fcmp-ui.js', find: '', put: deaf('wheel', 'true') }],
    groups: ['values', 'presets', 'modes'],
    red: ['values.wheel', 'presets.browser.wheel', 'modes.wheel'] },
  { name: 'deaf-drop',
    what: 'a dropped file is ignored',
    edits: [{ file: 'main.js', find: 'if (event.dataTransfer.files[0]) openFile(event.dataTransfer.files[0]);',
              put: '' }],
    groups: ['start', 'file'],
    red: ['start.early-drop', 'file.drop', 'file.bad', 'file.short'] },
  { name: 'stuck-source',
    what: 'BUILT-IN LOOP does nothing',
    edits: [{ file: 'main.js', find: "$('fcmp-loop').addEventListener('click', () => {",
              put: "$('fcmp-loop').addEventListener('fcmp-mutant', () => {" }],
    groups: ['file'],
    red: ['file.loop'] },
  { name: 'wrong-latency',
    what: 'the worklet reports one sample more than the engine has',
    edits: [{ file: 'fcmp-worklet.js', find: 'latency: this.ok ? this.x.fcmp_web_latency(this.engine) : 0 });',
              put: 'latency: this.ok ? this.x.fcmp_web_latency(this.engine) + 1 : 0 });' }],
    groups: ['quality'],
    red: ['quality.hq', 'quality.5ms', 'quality.20ms', 'quality.eco', 'quality.std', 'quality.off',
          'quality.settings'] },
  { name: 'stays-lost',
    what: 'the editor never hears that its WebGL context was restored',
    edits: [{ file: 'fcmp-ui.js', find: '', put: deaf('webglcontextrestored', 'true') }],
    groups: ['context'],
    red: ['context.still.restored', 'context.restored'] },
  { name: 'atlas-not-restored',
    what: 'after a restored context no texture is uploaded again: the font atlas is missing from the frame',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: '{ let restored = false;\n'
                   + "  window.addEventListener('webglcontextrestored', () => { restored = true; }, true);\n"
                   + "  for (const name of ['texImage2D', 'texSubImage2D', 'texStorage2D']) {\n"
                   + '    const real = WebGL2RenderingContext.prototype[name];\n'
                   + '    WebGL2RenderingContext.prototype[name] = function (...a) {\n'
                   + '      return restored ? undefined : real.apply(this, a);\n'
                   + '    };\n  }\n}\n' }],
    groups: ['context'],
    red: ['context.still.restored'] },
  { name: 'hidden-draws',
    what: 'the editor goes on ticking while its tab is hidden: it is not told, and its frames come from a worker',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: deaf('visibilitychange', 'true')
                   + "Object.defineProperty(Document.prototype, 'hidden', { get: () => false });\n"
                   + '{ const ticks = new Worker(URL.createObjectURL(new Blob('
                   + '["setInterval(() => postMessage(0), 16)"])));\n'
                   + '  let waiting = [];\n'
                   + '  ticks.onmessage = () => { const run = waiting; waiting = []; '
                   + 'for (const f of run) f(performance.now()); };\n'
                   + '  window.requestAnimationFrame = (f) => waiting.push(f);\n}\n' }],
    groups: ['hidden'],
    red: ['hidden.hidden'] },
  { name: 'no-storage',
    what: 'the preferences are never stored',
    edits: [{ file: 'fcmp-ui.js', find: '', put: 'Storage.prototype.setItem = function () {};\n' }],
    groups: ['zoom'],
    red: ['zoom.steps', 'zoom.reload', 'zoom.refit'] },
  { name: 'console-error',
    what: 'the page logs one error-level line, two seconds after it loads',
    edits: [{ file: 'main.js', find: '',
              put: "setTimeout(() => console.error('mutant: an error-level line'), 2000);\n" }],
    groups: ['start', 'screens'],
    red: ['errors.console'] },
  { name: 'uncaught',
    what: 'the page throws one uncaught error, two seconds after it loads',
    edits: [{ file: 'main.js', find: '',
              put: "setTimeout(() => { throw new Error('mutant: an uncaught error'); }, 2000);\n" }],
    groups: ['start', 'screens'],
    red: ['errors.console'] },
];

const usage = 'usage: mutants.mjs --dir <site> --out <dir> [--only <mutant>[,<mutant>...]] [--list] [--keep] '
            + '[--chrome <path>] [--chrome-flag <switch>]...';
const cannot = (why) => {
  console.error(`mutants: ${why}`);
  process.exit(2);
};

const opt = { dir: '', out: '', only: '', list: false, keep: false, pass: [] };
for (let i = 2; i < process.argv.length; i += 1) {
  const a = process.argv[i];
  const next = () => (i + 1 < process.argv.length ? process.argv[++i] : cannot(usage));
  if (a === '--dir') opt.dir = next();
  else if (a === '--out') opt.out = next();
  else if (a === '--only') opt.only = next();
  else if (a === '--list') opt.list = true;
  else if (a === '--keep') opt.keep = true;
  else if (a === '--chrome' || a === '--chrome-flag') opt.pass.push(a, next());
  else cannot(usage);
}
if (opt.list) {
  for (const m of MUTANTS) console.log(`${m.name.padEnd(20)} ${m.what}`);
  process.exit(0);
}
if (!opt.dir || !opt.out) cannot(usage);
const chosen = opt.only === '' ? MUTANTS : opt.only.split(',').map((name) => MUTANTS.find((m) => m.name === name)
  || cannot(`there is no mutant "${name}" (the mutants: ${MUTANTS.map((m) => m.name).join(', ')})`));
const dir = resolve(opt.dir);
const out = resolve(opt.out);
if (!existsSync(join(dir, 'index.html'))) cannot(`${dir} is not the site: it has no index.html`);
mkdirSync(out, { recursive: true });

// The mutated copy of the site at `to`.
function mutate(m, to) {
  rmSync(to, { recursive: true, force: true });
  cpSync(dir, to, { recursive: true });
  for (const edit of m.edits) {
    const file = join(to, edit.file);
    const text = readFileSync(file, 'utf8');
    const found = edit.find === '' ? 1 : text.split(edit.find).length - 1;
    if (found !== 1) {
      cannot(`the mutant ${m.name} does not apply: ${edit.file} has "${edit.find.trim()}" ${found} times, not once`);
    }
    writeFileSync(file, edit.find === '' ? edit.put + text : text.replace(edit.find, () => edit.put));
  }
}

// The scenario on the site at `site`: { code, output }.
const scenario = (site, m) => new Promise((ended) => {
  const args = [join(import.meta.dirname, '..', 'scenario.mjs'), '--dir', site, '--out', join(out, `${m.name}.run`),
                '--only', m.groups.join(','), ...(m.groups.includes('pictures') ? ['--png'] : []), ...opt.pass];
  const child = spawn(process.execPath, args, { stdio: ['ignore', 'pipe', 'pipe'] });
  let output = '';
  child.stdout.on('data', (chunk) => { output += chunk; });
  child.stderr.on('data', (chunk) => { output += chunk; });
  child.on('error', (error) => ended({ code: null, output: `${output}could not start: ${error.message}\n` }));
  child.on('close', (code, signal) => ended({ code: code ?? signal, output }));
});

let passed = 0;
for (const m of chosen) {
  const site = join(out, m.name);
  mutate(m, site);
  const t0 = Date.now();
  const { code, output } = await scenario(site, m);
  writeFileSync(join(out, `${m.name}.log`), output);
  if (!opt.keep) rmSync(site, { recursive: true, force: true });
  rmSync(join(out, `${m.name}.run`), { recursive: true, force: true });
  const rows = output.split('\n').map((line) => /^(PASS|FAIL)\s+scenario ([^\s:]+)/.exec(line)).filter((r) => r);
  const red = rows.filter((r) => r[1] === 'FAIL').map((r) => r[2]);
  const green = m.red.filter((name) => !red.includes(name));
  const ok = code === 1 && green.length === 0;
  if (ok) passed += 1;
  console.log(`${ok ? 'PASS' : 'FAIL'}     mutants ${m.name}: ${m.what}: the scenario ends with exit ${code} after `
              + `${((Date.now() - t0) / 1000).toFixed(0)} s, ${red.length} of ${rows.length} rows red`
              + `${green.length > 0 ? `; NOT red: ${green.join(', ')}` : ''}; red: ${red.join(', ') || 'none'}`);
}
console.log(`mutants: ${passed}/${chosen.length} passed`);
process.exit(passed === chosen.length ? 0 : 1);
