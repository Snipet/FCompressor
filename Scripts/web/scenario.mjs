#!/usr/bin/env node
// Scripts/web/scenario.mjs: the scripted user of the browser demo (ADR-93, the lead phase): the plan's hand checks as
// rows, in one headless Chrome on the built site. Scripts/web-live.sh runs it as part of the browser gate.
//
//   node Scripts/web/scenario.mjs --dir <site> --out <dir> [--png] [--chrome <path>]
//                                 [--only <group>[,<group>...]] [--list] [--timeout <s>] [--chrome-flag <switch>]...
//
//   --dir <site>      the site as it is shipped (build-web/site, or a downloaded copy); served from 127.0.0.1
//   --out <dir>       where results go: <dir>/png with --png. The rows are printed, not written. While it runs, a
//                     directory <dir>/scenario-* holds the browser's profile and the files dropped; it goes at the end
//   --png             also the group `pictures`: the six views in one stepped and one continuous Mode in GRAPHITE and
//                     the panel in PAPER, as PNGs of the canvas (never committed)
//   --chrome <path>   the Chrome to run (else the library's own choice)
//   --only <groups>   these groups only, in the order below (a group can always be run alone)
//   --list            the groups' names, one a line, and nothing else
//   --timeout <s>     the whole run's bound (default 480; the gate passes four times its own --timeout and stops the
//                     scenario at five times): past it the run ends with exit 2 and says in which group it was
//   --chrome-flag <switch>   one more switch for Chrome, through the library's own option (repeatable; the last of two
//                     wins). For a software renderer: --chrome-flag --use-angle=swiftshader --chrome-flag
//                     --enable-unsafe-swiftshader; for a machine with no audio device: --chrome-flag
//                     --disable-audio-output
//
// Chrome is always headless and muted, with a throwaway profile, and may start an AudioContext without a gesture (the
// rows need a running context; START is pressed all the same). Where no context renders through the machine's audio
// device (a runner that has none), Chrome is started again with its own null sink, and the NOTE `audio` says so.
// Input is real (Scripts/web/scenario/driver.mjs has the rules); a control is found by its title in the Panel's
// accessibility list, Module.fcmpA11y(); a row is judged by what reached the engine (the values of the reply it sent
// back, the worklet's counters, the editor's status) or by the Panel's own state, never by what the script meant to
// do. Nothing waits a fixed time for an outcome: every wait is on a condition and has a bound, and the NOTE `time`
// names the outcome that came nearest its bound on this machine and where in the groups it was waited for (of the
// waits bounded at 10 s or less; driver.mjs, u.slowest, says which waits those are and which are left out).
//
// The groups (Scripts/web/scenario/<group>.mjs; each file says what its rows are):
//   start      START: the context runs, telemetry arrives, nothing is refused
//   screens    every screen opens and closes: CHARACTERISTICS and its tabs, settings, the two browsers
//   values     a drag, a double click, the wheel, a typed value (accepted, refused, cancelled)
//   edits      undo and redo by the platform's chord and by the buttons; A|B and its menu
//   presets    next and previous, the browser, MODIFIED, SAVE AS with a typed name, rename, delete, the menus, IMPORT
//              and EXPORT
//   modes      a Mode change to one stepped and one continuous Mode, by the arrows, the wheel and the browser
//   quality    QUALITY and LOOKAHEAD: the latency in the status, the worklet and the reply; audio continues
//   file       a dropped file plays, a bad one leaves the source with its notice, BUILT-IN LOOP returns
//   context    a lost WebGL context recovers, and the still frame after it is SoftRaster's
//   hidden     the tab hidden and shown again: the pulls stop and resume, the audio goes on; RESUME
//   zoom       the zoom steps a window allows, and the preference after a reload
//   pictures   (--png only) the PNGs
//   errors     always last: no uncaught error and no error-level console line on the demo page, and on every page of
//              the run that was started, the demo still plays and every record the editor posted was taken by the
//              engine
// A group either starts on a page of its own (a new document, the origin's storage cleared, START pressed) or goes on
// with the page the group before it left, when that group passed and the demo there still plays; so a failing row
// names its group, and the groups after it are not judged on what it left behind. A demo that stopped between two
// groups (it played as the group before ended, and no longer does) is the next group's FAIL row `page`, and
// errors.records' (its ledger is taken then, after the group it followed); that group then goes on with a new page.
// An error of the driver inside a group is that group's FAIL row `driver`, and the group ends there.
//
// Output: PASS|FAIL|NOTE rows, then `scenario: N/M passed`. Exit 0 every row passed; 1 a row failed; 2 it could not
// run (usage, no site, no browser or one that went away, no verdict in time). No dependency; node 22 or later.
//
// That the rows can fail is shown by Scripts/web/scenario/mutants.mjs: this scenario on copies of the site with one
// thing broken in each.
import { existsSync } from 'node:fs';
import { join, resolve } from 'node:path';

import { Report } from './scenario/report.mjs';

const usage = 'usage: scenario.mjs --dir <site> --out <dir> [--png] [--chrome <path>] [--only <group>[,<group>...]] '
            + '[--list] [--timeout <s>] [--chrome-flag <switch>]...';
const cannot = (why) => {
  console.error(`scenario: ${why}`);
  process.exit(2);
};

// ---- arguments ------------------------------------------------------------------------------------------------------
const opt = { dir: '', out: '', png: false, chrome: '', only: '', list: false, timeoutS: 480, flags: [] };
{
  const takes = { '--dir': 'dir', '--out': 'out', '--chrome': 'chrome', '--only': 'only' };
  const next = (i) => (i < process.argv.length ? process.argv[i] : cannot(usage));
  for (let i = 2; i < process.argv.length; i += 1) {
    const a = process.argv[i];
    if (a in takes) opt[takes[a]] = next(++i);
    else if (a === '--png') opt.png = true;
    else if (a === '--list') opt.list = true;
    else if (a === '--timeout') opt.timeoutS = Number(next(++i));
    else if (a === '--chrome-flag') opt.flags.push(next(++i));
    else cannot(usage);
  }
}

const NAMES = ['start', 'screens', 'values', 'edits', 'presets', 'modes', 'quality', 'file', 'context', 'hidden',
               'zoom', 'pictures'];
if (opt.list) {
  for (const name of [...NAMES, 'errors']) console.log(name);
  process.exit(0);
}
if (!opt.dir || !opt.out || !(opt.timeoutS > 0)) cannot(usage);
const only = opt.only === '' ? null : opt.only.split(',');
for (const name of only || []) {
  if (!NAMES.includes(name)) {
    cannot(`there is no group "${name}" (the groups: ${NAMES.join(', ')}; errors always runs)`);
  }
}
if (Number(process.versions.node.split('.')[0]) < 22) cannot(`node 22 or later is needed (this is ${process.version})`);
const dir = resolve(opt.dir);
const out = resolve(opt.out);
for (const file of ['index.html', 'main.js', 'fcmp-ui.js', 'fcmp-ui.wasm', 'fcmp-worklet.js', 'fcmp-engine.wasm']) {
  if (!existsSync(join(dir, file))) cannot(`${dir} is not the site: it has no ${file}`);
}

// ---- the run --------------------------------------------------------------------------------------------------------
// --chrome reaches the library as its option `chrome`.
if (opt.chrome && !existsSync(opt.chrome)) cannot(`there is no browser at ${opt.chrome}`);
// Imported only now: the library starts nothing, but it takes the process's signals as it loads.
const { launch, user, OUTCOME_MS } = await import('./scenario/driver.mjs');
let over = false;                                     // the run's last line is out: nothing is printed after it
const say = (line) => {
  if (!over) console.log(line);
};
const report = new Report(say);
const chosen = NAMES.filter((name) => (only ? only.includes(name) : name !== 'pictures' || opt.png));
const groups = [];
for (const name of chosen) groups.push({ name, ...(await import(`./scenario/${name}.mjs`)) });
const errors = await import('./scenario/errors.mjs');

let session = null;
// The end, once: the browser is closed, and the exit waits for what was printed (a pipe to a runner may still hold
// the last rows).
const finish = async (code) => {
  if (over) return;
  over = true;
  if (session !== null) await session.close();
  process.stdout.write('', () => process.exit(code));
};
const watchdog = setTimeout(() => {
  say(`scenario: no verdict within ${opt.timeoutS} s (in the group ${report.group || 'none'})`);
  finish(2);
}, opt.timeoutS * 1000);

try {
  session = await launch({ dir, out, chromePath: opt.chrome, flags: opt.flags });
} catch (error) {
  cannot(`the run could not start (${error.message})`);
}

// An error as one line: what it says, and where in the scenario's own files it was thrown.
const trace = (error) => {
  const lines = String((error && error.stack) || error).split('\n');
  const at = lines.slice(1).map((line) => /\/Scripts\/web\/(scenario[^)\s]*)\)?$/.exec(line)).filter((m) => m)
    .slice(0, 2).map((m) => m[1]);
  return `${lines[0]}${at.length > 0 ? ` (at ${at.join(', ')})` : ''}`;
};

const t0 = Date.now();
let code = 2;
try {
  const u = await user(session.browser, session.base);
  const ledgers = [];                                 // one per page that ran, taken as its last group ended
  const t = { u, out, png: opt.png, scratch: session.scratch, row: report.row.bind(report),
              note: report.note.bind(report),
              // A group that opens a second page hands the first one's ledger over before it goes.
              leave: async () => ledgers.push({ after: report.group, ...(await errors.ledger(u)) }) };
  report.note('browser', await errors.browser(u));
  report.note('audio', session.audio);
  let passed = false;                                 // the group before left no failing row
  const running = async () => {
    try {
      return u.tapped() && (await u.look()).state === 'running';
    } catch {
      return false;
    }
  };
  for (const [index, group] of groups.entries()) {
    if (!session.alive()) throw new Error(`the browser went away (before the group ${group.name})`);
    if (!await u.there()) throw new Error(`the browser took the page's tab away (before the group ${group.name})`);
    // The page the group before left, for this group to go on with: the demo there must still play. One that played
    // as that group ended and no longer does stopped between the two: its ledger is taken now, after the group it
    // followed (errors.records judges it), and this group says so before it goes on with a new page.
    const goesOn = group.page === 'continue' && passed && u.tapped();
    const plays = goesOn && await running();
    let stopped = null;
    if (goesOn && !plays) {
      await t.leave();
      stopped = ledgers[ledgers.length - 1];
    }
    report.enter(group.name);
    try {
      if (stopped !== null) {
        report.row(false, 'page', `after the group ${stopped.after} and before this one: `
                                  + `${stopped.broken || 'a look at the page failed'}; this group goes on with a new `
                                  + 'page');
      }
      if (group.page === 'new' || (group.page === 'continue' && !plays)) {
        const loaded = await u.load();
        const started = loaded.ok ? await u.start() : { ok: false, why: 'the page did not come up' };
        if (!started.ok) throw new Error(`no running page to act on: ${started.why}`);
      }
      await group.run(t);
    } catch (error) {
      report.row(false, 'driver', trace(error));
    }
    passed = report.failedHere === 0;
    // This page's ledger, before a group that opens another one replaces it.
    const last = index + 1 === groups.length || groups[index + 1].page !== 'continue' || !passed;
    if (last) await t.leave();
  }
  report.enter('errors');
  await errors.run(t, ledgers);
  report.enter('');
  const near = u.slowest;
  report.note('time', `${((Date.now() - t0) / 1000).toFixed(1)} s for ${chosen.join(', ')}; of the waits for an `
                      + `outcome bounded at ${OUTCOME_MS / 1000} s or less, the one nearest its bound took ${near.ms} `
                      + `ms of ${near.bound}${near.at ? ` (at ${near.at})` : ''}`);
  say(report.summary());
  code = report.failed === 0 ? 0 : 1;
} catch (error) {
  say(`scenario: it could not run (${trace(error)})`);
}
clearTimeout(watchdog);
await finish(code);
