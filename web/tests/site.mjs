// web/tests/site.mjs: what the site says it was built from (ADR-93, web Sprint D).
//
// FCMP_WEB_TEST name=web.site timeout=120 on=web args={source},{build}
//
//   node site.mjs <repository> <build directory>
//
// The page's footer links the commit in built-from.txt as the source of what it runs, so that line may say "clean"
// only when everything in the two modules is that commit's: FCompressor's tree, and the FunkGui the build compiled
// (more than half of fcmp-ui.wasm) being the pinned commit itself. This script runs cmake/FcmpWebSite.cmake, the
// script the build ran, with the build's own inputs (<build>/site-args.cmake) into scratch directories:
//   built_from.as_built    the build's inputs give the line the built site carries (the time apart), and it is clean
//                          exactly when FCompressor's tree is clean (the FunkGui of a gate is the pin)
//   built_from.override    told that FunkGui is an override, the line says dirty
//   built_from.not_the_pin told another pinned commit than the one FunkGui is at, the line says dirty
//   built_from.changed     a FunkGui tree with an uncommitted file (a scratch clone at the pin) says dirty, and the
//                          same clone untouched does not
//   site.same_files        the scratch site holds exactly the built site's files
// Output: PASS/FAIL/NOTE lines, as the probes print them. Exit 0 pass, 1 fail, 2 usage.
import { spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const TEST = 'web.site';

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
  console.error('usage: node site.mjs <repository> <build directory>');
  process.exit(2);
}
const argsFile = join(build, 'site-args.cmake');
const script = join(source, 'cmake', 'FcmpWebSite.cmake');
if (!existsSync(argsFile) || !existsSync(join(build, 'site', 'built-from.txt'))) {
  row(false, 'inputs', `no ${argsFile} or no built site: build the web tree first (cmake --build --preset web)`);
  process.exit(1);
}
const argsText = readFileSync(argsFile, 'utf8');
const arg = (name) => (new RegExp(`set\\(${name} \\[==\\[(.*)\\]==\\]\\)`).exec(argsText) || [])[1];
const cache = readFileSync(join(build, 'CMakeCache.txt'), 'utf8');
const cmake = (/^CMAKE_COMMAND:INTERNAL=(.+)$/m.exec(cache) || [])[1];
const git = arg('GIT_EXECUTABLE');
const funkgui = arg('FCMP_FUNKGUI_DIR');
const pin = arg('FCMP_FUNKGUI_SHA');
if (!cmake || !git || !funkgui || !pin) {
  row(false, 'inputs', `site-args.cmake or CMakeCache.txt lacks a value (cmake ${cmake}, git ${git}, FunkGui ${funkgui}, pin ${pin})`);
  process.exit(1);
}

const scratch = mkdtempSync(join(tmpdir(), 'fcmp-site-'));
let made = 0;
// Runs the site script into a new scratch directory; returns built-from.txt's words, or the reason it failed.
function assemble(extra = []) {
  made += 1;
  const dir = join(scratch, `site${made}`);
  const run = spawnSync(cmake, [`-DFCMP_SITE_ARGS=${argsFile}`, `-DFCMP_SITE_DIR=${dir}`, ...extra, '-P', script],
                        { encoding: 'utf8' });
  if (run.status !== 0) return { dir, error: (run.stderr || run.stdout || 'no output').trim().slice(0, 400) };
  const line = readFileSync(join(dir, 'built-from.txt'), 'utf8');
  const words = /^site ([0-9a-f]{40}|none) (clean|dirty) (\S+)\n$/.exec(line);
  return words ? { dir, sha: words[1], state: words[2] } : { dir, error: `built-from.txt reads ${JSON.stringify(line)}` };
}
const gitOut = (dir, ...args) => {
  const run = spawnSync(git, ['--no-optional-locks', '-C', dir, ...args], { encoding: 'utf8' });
  return run.status === 0 ? run.stdout.trim() : null;
};
function files(dir, under = '') {
  return readdirSync(join(dir, under), { withFileTypes: true }).flatMap((e) =>
    (e.isDirectory() ? files(dir, join(under, e.name)) : [join(under, e.name)])).sort();
}

try {
  // ---- as built --------------------------------------------------------------------------------------------------
  const built = /^site ([0-9a-f]{40}|none) (clean|dirty) /.exec(readFileSync(join(build, 'site', 'built-from.txt'), 'utf8'));
  const base = assemble();
  const treeClean = gitOut(source, 'status', '--porcelain') === '';
  const overridden = arg('FCMP_FUNKGUI_OVERRIDE') === '1';
  const funkguiExact = !overridden && gitOut(funkgui, 'rev-parse', 'HEAD^{commit}') === pin
                       && gitOut(funkgui, 'status', '--porcelain') === '';
  const want = treeClean && funkguiExact ? 'clean' : 'dirty';
  row(!base.error && !!built && base.sha === built[1] && base.state === want, 'built_from.as_built',
      base.error || `${base.sha.slice(0, 12)} ${base.state} (want ${want}: FCompressor's tree is `
        + `${treeClean ? 'clean' : 'not clean'}, FunkGui is ${funkguiExact ? 'the pin' : 'not exactly the pin'}); `
        + `the built site says ${built ? built[1].slice(0, 12) + ' ' + built[2] : 'nothing readable'}`);
  if (!treeClean) note('FCompressor\'s tree has changes: every line says dirty, so the rows below judge only that');
  if (!funkguiExact) note(`FunkGui at ${funkgui} is not exactly the pin ${pin}${overridden ? ' (an override)' : ''}`);

  // ---- an override, another pin ------------------------------------------------------------------------------------
  const override = assemble(['-DFCMP_FUNKGUI_OVERRIDE=1']);
  row(!override.error && override.state === 'dirty', 'built_from.override',
      override.error || `with FCMP_FUNKGUI_OVERRIDE=1 the line says ${override.state}`);
  const other = assemble([`-DFCMP_FUNKGUI_SHA=${'0'.repeat(40)}`]);
  row(!other.error && other.state === 'dirty', 'built_from.not_the_pin',
      other.error || `with a pin FunkGui is not at, the line says ${other.state}`);

  // ---- a changed FunkGui tree: a scratch clone at the pin, clean and then with one untracked file ------------------
  const clone = join(scratch, 'funkgui');
  const cloned = spawnSync(git, ['clone', '--quiet', '--no-hardlinks', funkgui, clone], { encoding: 'utf8' });
  const atPin = cloned.status === 0
                && spawnSync(git, ['-C', clone, 'checkout', '--quiet', '--detach', pin], { encoding: 'utf8' }).status === 0;
  if (!atPin) {
    row(false, 'built_from.changed', `could not clone ${funkgui} at ${pin}: ${(cloned.stderr || '').trim().slice(0, 200)}`);
  } else {
    const untouched = assemble([`-DFCMP_FUNKGUI_DIR=${clone}`, '-DFCMP_FUNKGUI_OVERRIDE=0']);
    writeFileSync(join(clone, 'a-local-change.txt'), 'x\n');
    const changed = assemble([`-DFCMP_FUNKGUI_DIR=${clone}`, '-DFCMP_FUNKGUI_OVERRIDE=0']);
    const wantUntouched = treeClean ? 'clean' : 'dirty';
    row(!untouched.error && !changed.error && untouched.state === wantUntouched && changed.state === 'dirty',
        'built_from.changed',
        untouched.error || changed.error
          || `a clone at the pin says ${untouched.state} (want ${wantUntouched}); with one untracked file it says `
             + `${changed.state}`);
  }

  // ---- the same files ----------------------------------------------------------------------------------------------
  if (!base.error) {
    const a = files(join(build, 'site'));
    const b = files(base.dir);
    row(a.length > 0 && a.join('\n') === b.join('\n'), 'site.same_files',
        `${b.length} files from the script, ${a.length} in the built site`);
  }
} finally {
  rmSync(scratch, { recursive: true, force: true });
}

console.log(`${failed === 0 ? 'PASS' : 'FAIL'}     ${TEST}: ${passed} row(s) passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
