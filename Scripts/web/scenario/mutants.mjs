#!/usr/bin/env node
// Scripts/web/scenario/mutants.mjs: the proof that the scripted user's rows can fail. Each mutant is a copy of the
// site with one thing broken, by one text put into one shipped file; the scenario (Scripts/web/scenario.mjs) runs on
// it, and the rows that guard the broken thing must come out FAIL. Not part of the gate and not a group of the
// scenario: run by hand when a row or the page changes.
//
//   node Scripts/web/scenario/mutants.mjs --dir <site> --out <dir> [--only <mutant>[,<mutant>...]] [--list] [--keep]
//                                         [--timeout <s>] [--chrome <path>] [--chrome-flag <switch>]...
//
//   --dir <site>     the site as it is built (build-web/site): never written to
//   --out <dir>      <dir>/<mutant>.log is the scenario's output on that mutant; while it runs, <dir>/<mutant>/ is the
//                    mutated site and <dir>/<mutant>.run/ the scenario's --out. Both go when that scenario has ended,
//                    on every way out (the mutated site stays with --keep)
//   --only, --list   these mutants only; the mutants' names and what each breaks
//   --timeout <s>    each scenario's own bound (its --timeout; 480, its default): one that has not ended 30 s after
//                    it is stopped, and its mutant fails
//   --chrome, --chrome-flag   passed to the scenario
//
// A mutant passes when the scenario ends with exit 1 on it and every row the mutant names is red. One marked `exact`
// passes only when no other row is red besides: the rows it names are the ones meant for what it breaks, and no
// more (the mutants of the sample loop and its buttons are marked so). A text that is to be replaced must be in its
// file exactly once: every mutant chosen is worked out before anything is written, and when the page has changed so
// that one does not apply, the run ends there with exit 2, nothing copied, and the mutant must be written again. Each
// scenario runs in a process group of its own, with the Chrome it starts. SIGINT, SIGTERM and SIGHUP are passed on to
// it (its library then stops its Chrome); should it not have ended 10 s later, its whole group is killed; then its
// directories go, its log says it was interrupted, and the run ends with exit 2. Output: PASS|FAIL rows, then
// `mutants: N/M passed`. Exit 0, 1, or 2 (usage, no site, a mutant that does not apply, an interrupted run).
import { spawn } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

// At the top of the editor's script, before the module has a listener of its own: one of the window's, in the
// capture phase, hears an event first and can keep it from every other.
const deaf = (type, when) => `window.addEventListener('${type}', (e) => { if (${when}) e.stopImmediatePropagation(); `
                             + '}, true);\n';
const ON_CANVAS = "e.target && e.target.id === 'fcmp-canvas'";

// A function of the page made to do nothing inside one other function of web/main.js: `put` after that function's
// first line, it hides the page's own there.
const idle = (name, indent) => `${indent}const ${name} = () => {};\n`;

// { name, what, edits: [{ file, find ('' puts the text at the top of the file), put }], groups (the scenario's, run
// with --only), red (rows that must fail), exact (true: no other row may fail) }.
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
  { name: 'stuck-sample',
    what: 'SAMPLE LOOP does nothing',
    edits: [{ file: 'main.js', find: "$('fcmp-loop').addEventListener('click', () => {",
              put: "$('fcmp-loop').addEventListener('fcmp-mutant', () => {" }],
    groups: ['file', 'source'], exact: true,
    red: ['file.loop', 'source.back', 'source.again', 'source.ends', 'source.turn'] },
  { name: 'stuck-synth',
    what: 'SYNTH LOOP does nothing',
    edits: [{ file: 'main.js', find: "$('fcmp-synth').addEventListener('click', () => {",
              put: "$('fcmp-synth').addEventListener('fcmp-mutant', () => {" }],
    groups: ['source'], exact: true,
    red: ['source.synth', 'source.back', 'source.quick', 'source.turn'] },
  { name: 'synth-plays-sample',
    what: 'SYNTH LOOP plays the sample loop under the synth loop\'s name',
    edits: [{ file: 'main.js', find: "    play(synth(), 'synth', SAY.synth);\n  });",
              put: "    play(sampleBuffer || synth(), 'synth', SAY.synth);\n  });" }],
    groups: ['source'], exact: true,
    red: ['source.synth', 'source.quick'] },
  { name: 'sample-level',
    what: 'the sample loop plays at 0.95 of its own level (0.45 dB under it)',
    edits: [{ file: 'main.js', find: 'sampleBuffer = bufferOf(fitLoop(file, context.sampleRate));',
              put: 'const quieter = fitLoop(file, context.sampleRate);\n'
                   + '      sampleBuffer = bufferOf({ ...quieter, left: quieter.left.map((x) => 0.95 * x),\n'
                   + '                                right: quieter.right.map((x) => 0.95 * x) });' }],
    groups: ['source'], exact: true,
    red: ['source.sample'] },
  { name: 'no-sample-at-start',
    what: 'START never asks for the sample loop: the synth loop plays, as when the file is missing',
    edits: [{ file: 'main.js', find: '    const sample = loadSample();\n',
              put: '    const sample = Promise.resolve(null);\n' }],
    groups: ['start', 'file', 'source'], exact: true,
    red: ['file.drop', 'source.sample', 'source.synth', 'source.lost', 'source.kept', 'source.fault',
          'source.slow'] },
  { name: 'unfitted',
    what: 'the page plays the file\'s frames as they are, not fitted to the context\'s rate (341,420 frames at 48 kHz)',
    edits: [{ file: 'main.js', find: 'sampleBuffer = bufferOf(fitLoop(file, context.sampleRate));',
              put: 'sampleBuffer = bufferOf(file);' }],
    groups: ['file', 'source'], exact: true,
    red: ['file.loop', 'source.sample', 'source.back', 'source.again', 'source.turn'] },
  { name: 'start-round-cache',
    what: 'START\'s own request for the sample loop goes round the browser\'s cache, as only a retry should',
    edits: [{ file: 'main.js', find: "cache: sampleFailed ? 'reload' : 'default'", put: "cache: 'reload'" }],
    groups: ['source'], exact: true,
    red: ['source.sample'] },
  { name: 'retry-from-cache',
    what: 'a retry of the sample loop may be answered by the browser\'s cache, which may hold the answer that failed',
    edits: [{ file: 'main.js', find: "cache: sampleFailed ? 'reload' : 'default'", put: "cache: 'default'" }],
    groups: ['source'], exact: true,
    red: ['source.again'] },
  { name: 'silent-lost',
    what: 'START swallows a sample loop that did not load: the synth loop plays and no notice says why',
    edits: [{ file: 'main.js', find: '        notice(SAY.sampleLost);\n', put: '' }],
    groups: ['source'], exact: true,
    red: ['source.lost', 'source.slow'] },
  { name: 'silent-retry',
    what: 'a press on SAMPLE LOOP that fails says nothing: the notice goes on saying that it loads',
    edits: [{ file: 'main.js', find: '      notice(SAY.sampleUnchanged);\n', put: '' }],
    groups: ['source'], exact: true,
    red: ['source.again'] },
  { name: 'lost-fails-start',
    what: 'a sample loop that did not load fails START',
    edits: [{ file: 'main.js', find: '      const loaded = await sample;\n',
              put: '      const loaded = await sample;\n'
                   + "      if (loaded === null) throw new Error('mutant: no sample loop');\n" }],
    groups: ['source'], exact: true,
    red: ['source.lost', 'source.fault', 'source.ends', 'source.slow'] },
  { name: 'loop-disabled',
    what: 'SAMPLE LOOP is disabled while the sample loop is not loaded: nothing can try again',
    edits: [{ file: 'main.js', find: "loop: kind !== 'sample' || !sampleLoaded",
              put: "loop: kind !== 'sample' && sampleLoaded" }],
    groups: ['source'], exact: true,
    red: ['source.lost', 'source.again', 'source.ends', 'source.slow', 'source.turn'] },
  { name: 'notice-stays',
    what: 'the notice is not cleared when the sample loop has loaded and plays',
    edits: [{ file: 'main.js', find: "    notice('');\n    play(buffer, 'sample', SAY.sample);",
              put: "    play(buffer, 'sample', SAY.sample);" }],
    groups: ['source'], exact: true,
    red: ['source.again'] },
  { name: 'short-wait',
    what: 'START gives the sample loop 5 s, not 15',
    edits: [{ file: 'main.js', find: 'export const SAMPLE_MS = 15000;', put: 'export const SAMPLE_MS = 5000;' }],
    groups: ['source'], exact: true,
    red: ['source.slow'] },
  { name: 'turn-ignored',
    what: 'a load of the sample loop that ends takes its turn whatever was chosen since',
    edits: [{ file: 'main.js',
              find: "    const buffer = await loadSample();\n    if (turn !== opening || state !== 'running') return;",
              put: "    const buffer = await loadSample();\n    if (state !== 'running') return;" }],
    groups: ['source'], exact: true,
    red: ['source.turn'] },
  { name: 'synth-no-turn',
    what: 'SYNTH LOOP takes no turn: a load of the sample loop that still runs wins over it',
    edits: [{ file: 'main.js', find: "    opening += 1;\n    notice('');\n    play(synth(), 'synth', SAY.synth);",
              put: "    notice('');\n    play(synth(), 'synth', SAY.synth);" }],
    groups: ['source'], exact: true,
    red: ['source.turn'] },
  { name: 'synth-keeps-notice',
    what: 'SYNTH LOOP does not clear the notice',
    edits: [{ file: 'main.js', find: "    opening += 1;\n    notice('');\n    play(synth(), 'synth', SAY.synth);",
              put: "    opening += 1;\n    play(synth(), 'synth', SAY.synth);" }],
    groups: ['source'], exact: true,
    red: ['source.turn'] },
  { name: 'second-fades-first',
    what: 'a source chosen under 30 ms after another does not take its place: the first one begins, part of the way '
          + 'down its fade',
    edits: [{ file: 'main.js', find: "  if (started > now) return { at: started, old: 'drop' };\n", put: '' }],
    groups: ['source'], exact: true,
    red: ['source.quick'] },
  { name: 'kept-file-lost',
    what: 'a file dropped while START loads is forgotten',
    edits: [{ file: 'main.js', find: "    if (choice === 'keep') waiting = file;", put: '' }],
    groups: ['source'], exact: true,
    red: ['source.kept'] },
  { name: 'engine-fault-waits',
    what: 'an engine that stops while START loads does not give the sample loop\'s load up: START ends when that does',
    edits: [{ file: 'main.js', find: '  function stop(error) {\n',
              put: `  function stop(error) {\n${idle('giveUpSample', '    ')}` }],
    groups: ['source'], exact: true,
    red: ['source.fault'] },
  { name: 'editor-fault-waits',
    what: 'an editor that aborts while START loads does not give the sample loop\'s load up',
    edits: [{ file: 'main.js', find: '    editorLost = (error) => {\n',
              put: `    editorLost = (error) => {\n${idle('giveUpSample', '      ')}` }],
    groups: ['source'], exact: true,
    red: ['source.fault'] },
  { name: 'closed-as-paused',
    what: 'an audio context closed from outside is taken for one the browser paused',
    edits: [{ file: 'main.js', find: "    if (context.state === 'closed') {", put: '    if (false) {' }],
    groups: ['source'], exact: true,
    red: ['source.ends'] },
  { name: 'notice-survives-end',
    what: 'the notice stays when the demo ends',
    edits: [{ file: 'main.js', find: '  const silence = () => {\n',
              put: `  const silence = () => {\n${idle('notice', '    ')}` }],
    groups: ['source'], exact: true,
    red: ['source.ends'] },
  { name: 'load-survives-end',
    what: 'a load of the sample loop that runs is not given up when the demo ends',
    edits: [{ file: 'main.js', find: '  const silence = () => {\n',
              put: `  const silence = () => {\n${idle('giveUpSample', '    ')}` }],
    groups: ['source'], exact: true,
    red: ['source.ends'] },
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
  { name: 'stored-zoom',
    what: 'every page load finds a zoom of 100 % stored',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: "try { localStorage.setItem('FCompressor.uiZoom', '100'); } catch (e) { /* no storage */ }\n" }],
    groups: ['zoom'],
    red: ['zoom.default', 'zoom.reload'] },
  { name: 'stays-asleep',
    what: 'once its tab was hidden, the editor takes it for hidden ever after',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: '{ let slept = false;\n'
                   + "  document.addEventListener('visibilitychange', () => {\n"
                   + "    if (document.visibilityState === 'hidden') slept = true;\n  }, true);\n"
                   + "  Object.defineProperty(Document.prototype, 'hidden', "
                   + "{ get() { return slept || this.visibilityState === 'hidden'; } });\n}\n" }],
    groups: ['hidden'],
    red: ['hidden.shown'] },
  { name: 'dead-resume',
    what: 'a press on RESUME does nothing',
    edits: [{ file: 'main.js', find: "else if (state === 'running') context.resume().catch(() => {});", put: '' }],
    groups: ['context', 'hidden'],
    red: ['context.restored.still', 'hidden.resume'] },
  { name: 'draws-nothing',
    what: 'no draw call reaches WebGL: the canvas stays empty',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: "for (const name of ['drawArrays', 'drawElements', 'drawArraysInstanced', 'drawElementsInstanced']) "
                   + '{\n  WebGL2RenderingContext.prototype[name] = function () {};\n}\n' }],
    groups: ['context', 'pictures'],
    red: ['context.still', 'context.still.restored', 'context.restored.still', 'pictures.bus-g', 'pictures.clean'] },
  { name: 'double-quanta',
    what: 'the worklet counts every render quantum twice',
    edits: [{ file: 'fcmp-worklet.js', find: '    this.quanta += 1;\n', put: '    this.quanta += 2;\n' }],
    groups: ['start'],
    red: ['start.audio'] },
  { name: 'stops-between',
    what: 'the engine stops by itself while the demo plays, between two groups: here once its counters were read for '
          + 'the third time, which is at the end of the group start',
    edits: [{ file: 'fcmp-worklet.js', find: "      else if (data && data.fcmp === 'stats') this.stats();\n",
              put: "      else if (data && data.fcmp === 'stats') {\n"
                   + '        this.stats();\n'
                   + '        this.asked = (this.asked || 0) + 1;\n'
                   + '        if (this.asked === 3) {\n'
                   + "          this.port.postMessage({ fcmp: 'error', error: 'mutant: it stops' });\n"
                   + '        }\n'
                   + '      }\n' }],
    groups: ['start', 'screens'],
    red: ['screens.page', 'errors.records'] },
  { name: 'stray-records',
    what: 'the page posts a record of its own to the worklet every 20 ms: a Params record by its size and kind, '
          + 'and nothing the engine takes',
    edits: [{ file: 'main.js', find: '',
              put: 'setInterval(() => {\n'
                   + '  const node = globalThis.fcmpPage && globalThis.fcmpPage.node();\n'
                   + '  if (!node) return;\n'
                   + '  const record = new ArrayBuffer(140);\n'
                   + '  new Uint16Array(record, 6, 1)[0] = 1;\n'
                   + '  node.port.postMessage(record, [record]);\n'
                   + '}, 20);\n' }],
    groups: ['start', 'screens'],
    red: ['screens.nothing-edited', 'errors.records'] },
  { name: 'no-menu',
    what: 'a menu is taken away as soon as it is shown',
    edits: [{ file: 'fcmp-ui.js', find: '',
              put: 'new MutationObserver(() => {\n'
                   + "  for (const menu of document.querySelectorAll('[data-funkgui-menu]')) menu.remove();\n"
                   + '}).observe(document.documentElement, { childList: true, subtree: true });\n' }],
    groups: ['edits', 'presets'],
    red: ['edits.ab.menu', 'presets.menu.save'] },
  { name: 'deaf-letters',
    what: 'no letter key reaches the editor',
    edits: [{ file: 'fcmp-ui.js', find: '', put: deaf('keydown', '/^[a-z]$/i.test(e.key)') }],
    groups: ['presets'],
    red: ['presets.save-as'] },
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
            + '[--timeout <s>] [--chrome <path>] [--chrome-flag <switch>]...';
const cannot = (why) => {
  console.error(`mutants: ${why}`);
  process.exit(2);
};

const opt = { dir: '', out: '', only: '', list: false, keep: false, timeoutS: 480, pass: [] };
for (let i = 2; i < process.argv.length; i += 1) {
  const a = process.argv[i];
  const next = () => (i + 1 < process.argv.length ? process.argv[++i] : cannot(usage));
  if (a === '--dir') opt.dir = next();
  else if (a === '--out') opt.out = next();
  else if (a === '--only') opt.only = next();
  else if (a === '--list') opt.list = true;
  else if (a === '--keep') opt.keep = true;
  else if (a === '--timeout') opt.timeoutS = Number(next());
  else if (a === '--chrome' || a === '--chrome-flag') opt.pass.push(a, next());
  else cannot(usage);
}
if (opt.list) {
  for (const m of MUTANTS) console.log(`${m.name.padEnd(20)} ${m.what}`);
  process.exit(0);
}
if (!opt.dir || !opt.out || !(opt.timeoutS > 0)) cannot(usage);
const chosen = opt.only === '' ? MUTANTS : opt.only.split(',').map((name) => MUTANTS.find((m) => m.name === name)
  || cannot(`there is no mutant "${name}" (the mutants: ${MUTANTS.map((m) => m.name).join(', ')})`));
const dir = resolve(opt.dir);
const out = resolve(opt.out);
if (!existsSync(join(dir, 'index.html'))) cannot(`${dir} is not the site: it has no index.html`);

// Every chosen mutant's files as they will be, from the site's own, before anything is written: { m, files (name ->
// text) }. One that does not apply ends the run here.
const planned = chosen.map((m) => {
  const files = new Map();
  for (const edit of m.edits) {
    let text = files.get(edit.file);
    if (text === undefined) {
      try {
        text = readFileSync(join(dir, edit.file), 'utf8');
      } catch {
        cannot(`the mutant ${m.name} does not apply: the site has no ${edit.file}`);
      }
    }
    const found = edit.find === '' ? 1 : text.split(edit.find).length - 1;
    if (found !== 1) {
      cannot(`the mutant ${m.name} does not apply: ${edit.file} has "${edit.find.trim()}" ${found} times, not once`);
    }
    files.set(edit.file, edit.find === '' ? edit.put + text : text.replace(edit.find, () => edit.put));
  }
  return { m, files };
});
mkdirSync(out, { recursive: true });

const STOP_MS = 10000;                                // a scenario told to stop has this long, then its group is killed
const GRACE_S = 30;                                   // past its own bound, a scenario that has not ended is stopped
const LATE = Symbol('late');
const within = (ms, promise) => {
  let timer = 0;
  return Promise.race([promise, new Promise((r) => { timer = setTimeout(() => r(LATE), ms); })])
    .finally(() => clearTimeout(timer));
};
const signal = (pid, sig) => {
  try {
    process.kill(pid, sig);
  } catch { /* gone */ }
};

// The mutant whose scenario runs: { site, run (its --out), child, done (resolves { code, output } once it has
// closed) }; null between two.
let current = null;
let interrupted = '';                                 // the signal that ended the run
let hung = false;                                     // the scenario overran its bound and was stopped

// Stops the current scenario: `sig` to it, whose library then kills its Chrome by pid and whose driver removes its
// scratch; should it not have closed within STOP_MS, its process group is killed whole (the Chrome it started is in
// it). The group is signalled only while the scenario lives, when its number is still its own.
const stop = async (c, sig) => {
  signal(c.child.pid, sig);
  if (await within(STOP_MS, c.done) !== LATE) return;
  signal(-c.child.pid, 'SIGKILL');
  await within(STOP_MS, c.done);
};
for (const sig of ['SIGINT', 'SIGTERM', 'SIGHUP']) {
  process.on(sig, () => {
    if (interrupted !== '') return;
    interrupted = sig;
    if (current === null) {                           // no scenario runs, and nothing of a mutant is on disk
      console.log(`mutants: interrupted by ${sig}`);
      process.exit(2);
    }
    stop(current, sig);                               // the loop below ends the run once the scenario has closed
  });
}
// An exit with a scenario still running (an error of this script): its group is killed and its directories go.
const removeCurrent = () => {
  if (current === null) return;
  if (current.child.exitCode === null && current.child.signalCode === null) signal(-current.child.pid, 'SIGKILL');
  if (!opt.keep) rmSync(current.site, { recursive: true, force: true });
  rmSync(current.run, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
};
process.on('exit', removeCurrent);

// The scenario on the mutated site at `site`, in a process group of its own: `current` while it runs.
const scenario = (site, run, m) => {
  const args = [join(import.meta.dirname, '..', 'scenario.mjs'), '--dir', site, '--out', run, '--only',
                m.groups.join(','), ...(m.groups.includes('pictures') ? ['--png'] : []), '--timeout',
                String(opt.timeoutS), ...opt.pass];
  const child = spawn(process.execPath, args, { stdio: ['ignore', 'pipe', 'pipe'], detached: true });
  let output = '';
  child.stdout.on('data', (chunk) => { output += chunk; });
  child.stderr.on('data', (chunk) => { output += chunk; });
  const done = new Promise((ended) => {
    child.on('error', (error) => ended({ code: null, output: `${output}could not start: ${error.message}\n` }));
    child.on('close', (code, sig) => ended({ code: code ?? sig, output }));
  });
  current = { site, run, child, done };
  const c = current;
  const timer = setTimeout(() => {
    hung = true;
    stop(c, 'SIGTERM');
  }, (opt.timeoutS + GRACE_S) * 1000);
  return done.finally(() => clearTimeout(timer));
};

let passed = 0;
for (const { m, files } of planned) {
  const site = join(out, m.name);
  const run = join(out, `${m.name}.run`);
  rmSync(site, { recursive: true, force: true });
  cpSync(dir, site, { recursive: true });
  for (const [name, text] of files) writeFileSync(join(site, name), text);
  const t0 = Date.now();
  hung = false;
  const { code, output } = await scenario(site, run, m);
  const ended = interrupted !== '' ? `mutants: interrupted by ${interrupted}\n`
              : hung ? `mutants: no end within ${opt.timeoutS + GRACE_S} s: stopped\n` : '';
  writeFileSync(join(out, `${m.name}.log`), output + ended);
  removeCurrent();
  current = null;
  if (interrupted !== '') {
    console.log(`mutants: interrupted by ${interrupted} (in the mutant ${m.name}): ${passed} passed before it`);
    process.exit(2);
  }
  const rows = output.split('\n').map((line) => /^(PASS|FAIL)\s+scenario ([^\s:]+)/.exec(line)).filter((r) => r);
  const red = rows.filter((r) => r[1] === 'FAIL').map((r) => r[2]);
  const green = m.red.filter((name) => !red.includes(name));
  const more = m.exact ? red.filter((name) => !m.red.includes(name)) : [];
  const ok = !hung && code === 1 && green.length === 0 && more.length === 0;
  if (ok) passed += 1;
  console.log(`${ok ? 'PASS' : 'FAIL'}     mutants ${m.name}: ${m.what}: the scenario ends with exit ${code} after `
              + `${((Date.now() - t0) / 1000).toFixed(0)} s${hung ? ' (it overran its bound and was stopped)' : ''}, `
              + `${red.length} of ${rows.length} rows red`
              + `${green.length > 0 ? `; NOT red: ${green.join(', ')}` : ''}`
              + `${more.length > 0 ? `; red and NOT named: ${more.join(', ')}` : ''}`
              + `; red: ${red.join(', ') || 'none'}`);
}
console.log(`mutants: ${passed}/${chosen.length} passed`);
process.exit(passed === chosen.length ? 0 : 1);
